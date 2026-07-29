/* m2_log.h — per-module file logging for Mercs2 ASI mods.
 *
 * Opens "<module>.log" next to the calling .asi. Every mod gets its OWN file even though the logger
 * lives in one shared DLL: the handle is keyed by the caller's HMODULE.
 *
 * `m2_logf` and `m2_log_close` are macros that supply that HMODULE for you, from the linker's
 * `__ImageBase`, so call sites are unchanged from when the SDK was compiled into each mod:
 *
 *     m2_log_init(hinstDll);          // in DllMain
 *     m2_logf("started, mode=%d", n); // lands in <your mod>.log, not someone else's
 *
 * Use the `_mod` forms directly only when logging on behalf of a module that is not the caller.
 */
#ifndef M2_LOG_H
#define M2_LOG_H

#include "m2_api.h"

#include <windows.h>

/* Open <module>.log next to the given module (typically the mod's HINSTANCE).
 * Calling twice for one module is a no-op rather than a second handle. */
M2_API void m2_log_init(HMODULE module);

/* Append a line (CRLF added automatically). No-op if that module never called m2_log_init. */
M2_API void m2_logf_mod(HMODULE module, const char* fmt, ...);

/* Close and forget this module's log. */
M2_API void m2_log_close_mod(HMODULE module);

/* Build "<module dir>\<filename>" into out. Useful for .ini paths etc. */
M2_API void m2_module_path(HMODULE module, const char* filename, char* out, int out_size);

/* Source-compatible spellings: route to the calling module automatically. */
#define m2_logf(...)   m2_logf_mod(M2_SELF_MODULE, __VA_ARGS__)
#define m2_log_close() m2_log_close_mod(M2_SELF_MODULE)

#endif /* M2_LOG_H */
