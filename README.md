# `m2` — the Mercenaries 2 mod SDK

**One shared layer, loaded once into the game process.** Mods `#include <m2.h>` and link against
`m2-sdk.dll`; they do not each carry a private copy.

```sh
git submodule add https://github.com/Mercenaries-Fan-Build/mercs2-sdk sdk
git submodule update --init --recursive   # in an existing clone
make -C sdk build                         # produces sdk/build/m2-sdk.dll + import library
```

## Why one instance and not a copy per mod

The layer owns state that is only correct if it is genuinely singular:

- **MinHook's allocator and hook table.** Installing a hook `Freeze()`s the process — enumerating
  every thread and rewriting its instruction pointer. With a copy compiled into each `.asi`, N mods
  meant N allocators doing that concurrently from N worker threads, unable to see each other's
  hooks. One instance removes the whole class of problem.
- **The subscription to the game's log stub.** One detour on that address, fanned out to every
  listener, rather than N mods each detouring it or each polling `pmc_blackbox.log` on its own
  300 ms thread.
- **The world-load ladder.** Every mod agrees on which phase the load has reached.

## The three things sharing changed

Splitting one copy per mod into one instance for all of them broke assumptions that were safe
before. Each was a *silent* failure, which is why they are called out here:

| was | now |
|---|---|
| `m2_log` kept a single `static HANDLE`. Fine with one mod per copy; shared, mod B's `m2_log_init` would clobber mod A's handle and every line would land in one file. | The handle is keyed by the caller's `HMODULE`. **No API change** — `m2_log_init` already took the module, and `m2_logf` is a macro that supplies `M2_SELF_MODULE`. |
| `m2_loghook` / `m2_loadtrigger` incremented their registry counts **non-atomically**. | Interlocked slot claim, callback published last. Lock-free on purpose: registration happens inside a mod's `DllMain`, under the loader lock, where taking a lock can deadlock. |
| A trigger registered *after* its phase had passed **never fired**. Rare when every mod registered from its own `DllMain`; shared, it is the normal case for the 2nd..Nth mod. | Already-passed phases fire immediately, exactly once, whichever thread gets there first. |

## ⚠ `m2-sdk.dll` is a load-time dependency

`m2-sdk.dll` lives in the **game root**, and the m2-sdk Shipment is what puts it there (see
[below](#the-m2-sdk-shipment)). Do not ship your own copy beside your `.asi`: a Shipment that needs
the SDK declares m2-sdk as a requirement instead. If the DLL is missing or the wrong architecture
the mod **does not load at all**: `LoadLibrary` fails with `0x8007007E` before any of your code
runs, so the mod cannot report the problem itself. pmc_bb logs only `[FAILED] <name> (error: 0x...)`.

Guard the other mismatch — a mod built against a newer header than the DLL it finds — in `DllMain`:

```c
if (!m2_abi_ok()) return FALSE;   /* refuse to load rather than misbehave */
```

The loader binds imports by **name only**, so a changed signature links happily and corrupts the
stack with no diagnostic. `m2_abi_ok()` compares the header's `M2_VERSION_NUM` against the DLL's
`m2_version_num()`.

## The m2-sdk Shipment

m2-sdk is published as a Quartermaster Shipment, described by [`manifest.yaml`](manifest.yaml). Its
one contribution is an `add_runtime_dll`, which places `m2-sdk.dll` in the game root, where every
`.asi` that imports it finds it. The Shipment is runtime only: nothing runs until a Shipment that
requires m2-sdk loads.

A Shipment whose `.asi` links the SDK declares the dependency in its own manifest:

```yaml
requires: [{ shipment: m2-sdk, version: "^0.2" }]
```

Each release attaches the Shipment as `m2-sdk-v<version>.zip`, beside `m2-sdk.dll`, the import
library and the headers. Releases are cut by pushing a `v*` tag, and the committed `manifest.yaml`
must already carry that version: the release stops if `qm manifest-info` reports anything else.

`manifest.yaml` declares `src/m2-sdk.dll`, which is a copy of the build output. `src/` is gitignored
and never committed. CI and the release stage it after `make build`. To run `qm` locally, stage it
yourself first, because qm refuses a declared file that does not exist:

```sh
make build
mkdir -p src && cp build/m2-sdk.dll src/
qm lint .
qm manifest-info manifest.yaml
```

The qm release CI uses is pinned in [`.github/qm-version`](.github/qm-version).

## Modules

| Header | What it gives you |
| --- | --- |
| [`m2_api.h`](m2/m2_api.h) | Linkage (`M2_API`) and `M2_SELF_MODULE`, the caller's own `HMODULE` via `__ImageBase`. |
| [`m2_version.h`](m2/m2_version.h) | The SDK's semver and the `m2_abi_ok()` load-time guard. |
| [`m2_target.h`](m2/m2_target.h) | All binary-specific addresses for the target EXE in one place (log stub, VO bindings, shader registration, section VAs). |
| [`m2_log.h`](m2/m2_log.h) | Per-module `<mod>.log` logging (`m2_log_init` / `m2_logf`). |
| [`m2_ini.h`](m2/m2_ini.h) | Tiny callback-based INI reader (`m2_ini_parse`, `m2_ini_bool/int`). |
| [`m2_hook.h`](m2/m2_hook.h) | SecuROM-safe `.text` detours via MinHook (`m2_hook_attach`). |
| [`m2_luastack.h`](m2/m2_luastack.h) | Bounds-checked reads of a Lua 5.1 (float-build) C-function's string args. |
| [`m2_loghook.h`](m2/m2_loghook.h) | One subscription to the game's whole log stream. |
| [`m2_loadtrigger.h`](m2/m2_loadtrigger.h) | Fire callbacks as the world load crosses loadprobe milestones. |
| [`m2_shader.h`](m2/m2_shader.h) | Register new shaders in the game's own shader registry (the `shader-registry` capability). |

**`.text` MinHook, never `.rdata`.** The cracked retail EXE tolerates code detours but anti-tampers
registration-table writes — a `.rdata` slot patch crashed early init under SecuROM. `m2_hook` routes
everything through MinHook.

## Using it from a mod

```make
include ../../sdk/sdk.mk
mod.asi: mod.c
	i686-w64-mingw32-gcc -O2 -shared $(M2_CFLAGS) -o $@ $< $(M2_LDFLAGS) -lkernel32 -luser32
```

```c
#include "m2.h"

static void on_world_load(int phase, void* ud) { /* arm your feature */ }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!m2_abi_ok()) return FALSE;
        m2_log_init(h);
        m2_hook_init();
        m2_loadtrigger_on_phase(M2_PHASE_ENTERED_WORLD_IDX, on_world_load, NULL);
        m2_loadtrigger_install();
    }
    return TRUE;
}
```

Link with `$(M2_LDFLAGS)`, **not** `$(M2_SRCS)`. `M2_SRCS` still exists so the SDK can build itself;
a mod that compiles it in gets a second private MinHook and a second log-stub hook, which is exactly
what this design exists to prevent.

[`test/consumer`](test/consumer/) is a complete minimal consumer, and `make -C test/consumer verify`
asserts the built `.asi` really imports `m2-sdk.dll` — compiling proves the headers agree, but only
the import table proves the binding.

[`test/shader_core`](test/shader_core/) tests the shader registry's platform-independent core with
the host compiler: `make -C test/shader_core check`.

## Registering shaders

[`m2_shader.h`](m2/m2_shader.h) adds new pixel and vertex shaders to the game's own shader registry,
through the same engine call retail uses for its built-in shaders. The m2-sdk Shipment declares
`provides: [shader-registry]`, so a Shipment whose `.asi` uses it requires the capability:

```yaml
load:
  requires:
    - { capability: shader-registry }
```

```c
m2_shader_status m2_shader_add_pixel(m2_shader_family family, const m2_shader_class classes[4]);
m2_shader_status m2_shader_add_vertex(m2_shader_family family, const m2_shader_class* cls);
m2_shader_status m2_shader_outcome(const char* name);
const char*      m2_shader_status_name(m2_shader_status s);
```

**When to call.** Call the add functions from your `DllMain` on `DLL_PROCESS_ATTACH`. The game builds
its registry once, in `FUN_0084f130`, which the renderer constructor (`FUN_007492d0`) calls during
startup; a plugin's `DllMain` runs before that. The first add call checks the signatures of the
engine functions involved, checks that the registry is empty, and installs a MinHook detour on
`FUN_0084f130`. The detour runs the game's own registration first, then registers every queued
entry, in queue order.

```c
static const m2_shader_class kGlow[4] = {
    { "MyGlowFP",       "MyGlowFP.sho" },
    { "MyGlowFP_pl",    "MyGlowFP_pl.sho" },
    { "MyGlowFP_sl",    "MyGlowFP_sl.sho" },
    { "MyGlowFP_pl_sl", "MyGlowFP_pl_sl.sho" },
};
static const m2_shader_class kGlowVs = { "MyGlowVP", "MyGlowVP.sho" };

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!m2_abi_ok()) return FALSE;
        m2_log_init(h);
        m2_shader_status st = m2_shader_add_pixel(M2_SHADER_FAMILY_BLUR_PIXEL, kGlow);
        if (st != M2_SHADER_OK) m2_logf("glow: %s", m2_shader_status_name(st));
        st = m2_shader_add_vertex(M2_SHADER_FAMILY_VERTEX, &kGlowVs);
        if (st != M2_SHADER_OK) m2_logf("glow vs: %s", m2_shader_status_name(st));
    }
    return TRUE;
}
```

The name and sho strings are read when the registry is built, after `DllMain` returns, so they must
live for the whole process. String literals do.

**Families.** `m2_shader_family` names the 45 record families in the retail EXE (26 pixel, 19
vertex), one per vtable. The SDK holds each family's vtable, record size and stage as data. A
record is built the way the game's static initializers build theirs: zeroed memory of the family's
size, the stage's base constructor (`FUN_0085ace0` for pixel, `FUN_0085ade0` for vertex), then the
family vtable at `+0`, then `FUN_0085ac90(record, name, sho, class)`.

**Classes.** A pixel entry carries 4 classes: plain, `_pl`, `_sl` and `_pl_sl`, registered with
class 0-3 in that order, so they take consecutive indices. Class 0 always registers. Classes 1-3
register only when the game's ShaderLevel byte (`0x00dfc345`) is non-zero, which is what retail
does. A vertex entry carries one class, registered as class 0.

**Statuses.** Every function returns a status; none exits or aborts.

| status | from | meaning |
|---|---|---|
| `M2_SHADER_OK` | all | queued, or (from `m2_shader_outcome`) registered |
| `M2_SHADER_ERR_SIGNATURE` | add | `FUN_0084f130`, `FUN_0085ac90`, `FUN_0085ace0` or `FUN_0085ade0` does not start with the bytes in `m2_target.h`: a different EXE build |
| `M2_SHADER_ERR_HOOK` | add | MinHook could not install the detour |
| `M2_SHADER_ERR_TOO_LATE` | add | the game has started its registry (a pool count is non-zero, or the detour has run) |
| `M2_SHADER_ERR_ARGUMENT` | add, outcome | a null or empty name or sho, or a sho that does not end in `.sho` or is over 128 characters; from `m2_shader_outcome`, a name that was never queued or that the registry has not reached |
| `M2_SHADER_ERR_DUPLICATE` | add, outcome | a name already queued or repeated within one pixel entry; from `m2_shader_outcome`, a name the game's registry already held |
| `M2_SHADER_ERR_FAMILY` | add | not a family, or a family of the other stage |
| `M2_SHADER_ERR_CAPACITY` | add, outcome | the registry holds 0x800 pixel and 0x100 vertex names; the live count plus the entry's registrations exceeds that |

Names compare by the engine's case-folded hash (`FUN_00824270`), so `MyGlowFP` and `myglowfp` are
one name. Each entry registers or fails on its own: an entry that fails leaves the others
registered, its names report the error through `m2_shader_outcome`, and one line naming the entry
and the status goes to the log of the module that queued it (`m2_log_init`). All the names of a
pixel entry share the entry's outcome.

## The world-load ladder (generated)

[`m2/load_ladder.gen.h`](m2/load_ladder.gen.h) and `load_ladder.gen.c` are generated from
loadprobe's `phases.rs` by [`gen_ladder.py`](gen_ladder.py) — **do not edit them by hand**. The
header carries the constants a mod uses; the table itself is compiled into the DLL, because a
`static` array in a header is duplicated into every consumer and cannot be exported.

```sh
make ladder        # regenerate from a sibling loadprobe checkout
make ladder-check  # drift guard: fail if either file is stale
make ladder PHASES=/path/to/phases.rs   # if loadprobe lives elsewhere
```

CI runs `ladder-check` against `mercs2-wad-simulator`'s `crates/loadprobe`. It has to: the drift is
**silent in the game** — `m2_loadtrigger` simply stops matching any milestone loadprobe has renamed,
and no mod reports anything.

## Target

Built for the cracked retail EXE (`53,482,288` bytes, sha256 `958eb227…`, image base `0x00400000`);
the addresses in `m2_target.h` are binary-specific. For that build
`file_offset == VA - 0x400000` holds across all 13 sections — do **not** assume that for retail v1.1
(`53,944,080` bytes), whose SecuROM sections differ.

⚠ `m2_version.h` versions the **SDK**, not the game it targets. Those are separate axes: an SDK
release can change without the target build changing, and vice versa.

MinHook is vendored under [`minhook/`](minhook/) (32-bit sources only) and carries its own
[license](minhook/LICENSE.txt); the SDK itself is [MIT](LICENSE).
