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

Ship it beside your `.asi`. If it is missing or the wrong architecture the mod **does not load at
all**: `LoadLibrary` fails with `0x8007007E` before any of your code runs, so the mod cannot report
the problem itself. pmc_bb logs only `[FAILED] <name> (error: 0x...)`.

Guard the other mismatch — a mod built against a newer header than the DLL it finds — in `DllMain`:

```c
if (!m2_abi_ok()) return FALSE;   /* refuse to load rather than misbehave */
```

The loader binds imports by **name only**, so a changed signature links happily and corrupts the
stack with no diagnostic. `m2_abi_ok()` compares the header's `M2_VERSION_NUM` against the DLL's
`m2_version_num()`.

## Modules

| Header | What it gives you |
| --- | --- |
| [`m2_api.h`](m2/m2_api.h) | Linkage (`M2_API`) and `M2_SELF_MODULE`, the caller's own `HMODULE` via `__ImageBase`. |
| [`m2_version.h`](m2/m2_version.h) | The SDK's semver and the `m2_abi_ok()` load-time guard. |
| [`m2_target.h`](m2/m2_target.h) | All binary-specific addresses for the target EXE in one place (log stub, VO bindings, section VAs). |
| [`m2_log.h`](m2/m2_log.h) | Per-module `<mod>.log` logging (`m2_log_init` / `m2_logf`). |
| [`m2_ini.h`](m2/m2_ini.h) | Tiny callback-based INI reader (`m2_ini_parse`, `m2_ini_bool/int`). |
| [`m2_hook.h`](m2/m2_hook.h) | SecuROM-safe `.text` detours via MinHook (`m2_hook_attach`). |
| [`m2_luastack.h`](m2/m2_luastack.h) | Bounds-checked reads of a Lua 5.1 (float-build) C-function's string args. |
| [`m2_loghook.h`](m2/m2_loghook.h) | One subscription to the game's whole log stream. |
| [`m2_loadtrigger.h`](m2/m2_loadtrigger.h) | Fire callbacks as the world load crosses loadprobe milestones. |

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
