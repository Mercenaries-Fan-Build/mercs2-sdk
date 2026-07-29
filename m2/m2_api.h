/* m2_api.h — linkage for the shared m2 layer.
 *
 * `m2` ships as ONE `m2-sdk.dll` loaded into the game process, not as a copy compiled into every
 * mod. Mods bind to it through the PE import table, so the Windows loader resolves it before the
 * mod's `DllMain` runs and plugin load order stops mattering.
 *
 * Why one instance rather than N: the layer owns process-global state that only works if it is
 * genuinely singular —
 *
 *   - MinHook's allocator and hook table. Each statically-linked copy `Freeze()`s the process,
 *     enumerating and rewriting every thread's instruction pointer to install a hook. N copies doing
 *     that concurrently from N worker threads is a deadlock and corruption risk, and the copies
 *     cannot see each other's hooks.
 *   - The subscription to the game's shared log stub. One detour, fanned out to every listener,
 *     instead of N mods each detouring the same address or each polling `pmc_blackbox.log`.
 *   - The world-load ladder state, so every mod agrees on which phase the load has reached.
 *
 * ⚠ Consequence a consumer must plan for: `m2-sdk.dll` becomes a hard load-time dependency. If it
 * is absent or the wrong architecture, the mod does not load at all — `LoadLibrary` fails with
 * `0x8007007E` before any of the mod's code runs, so the mod cannot report the problem itself.
 * pmc_bb logs the failure as `[FAILED] <name> (error: 0x...)`. Ship the DLL alongside the mod.
 */
#ifndef M2_API_H
#define M2_API_H

#include <windows.h>

/* M2_BUILDING_DLL is defined only when compiling the SDK itself (see the Makefile). */
#ifdef M2_BUILDING_DLL
#define M2_API __declspec(dllexport)
#else
#define M2_API __declspec(dllimport)
#endif

/* The calling module's own HMODULE, resolved at link time with no API call.
 *
 * The PE header sits at the image base, so the linker-provided `__ImageBase` symbol IS this
 * module's HMODULE. This is what lets per-caller APIs stay source-compatible: `m2_logf` can route to
 * the right mod's log file without every call site growing a module argument.
 */
extern IMAGE_DOS_HEADER __ImageBase;
#define M2_SELF_MODULE ((HMODULE)&__ImageBase)

#endif /* M2_API_H */
