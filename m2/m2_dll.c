/* m2_dll.c — the shared layer's own entry point and ABI guard.
 *
 * Deliberately does almost nothing at DLL_PROCESS_ATTACH. The Windows loader resolves this DLL as
 * an import of each mod, so this runs nested inside the loader lock, before the importing mod's own
 * DllMain. Anything that hooks, spawns a thread, or touches the game here would run at the least
 * predictable moment in the process's life — and every SDK facility is already lazily initialised
 * by its first caller.
 */
#include "m2_api.h"
#include "m2_version.h"

/* The compiled-in version of whoever asks, compared against the DLL's own.
 *
 * With the SDK compiled into each mod, header and implementation could not disagree. Now they can:
 * a mod built against m2 0.2 loading against an 0.1 DLL calls exports that may not exist or may
 * have changed meaning. The mismatch is otherwise invisible — the loader binds by NAME only, so a
 * changed signature links happily and corrupts the stack.
 *
 * Consumers call m2_abi_ok() from DllMain and bail loudly if it returns 0.
 */
M2_API int m2_version_num(void) {
    return M2_VERSION_NUM;
}

M2_API const char* m2_version_string(void) {
    return M2_VERSION_STRING;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* No thread notifications: nothing here is per-thread, and every mod that loads us would
         * otherwise pay a callback per thread the game creates. */
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
