/* A minimal consumer, built exactly the way a real mod is.
 *
 * This exists to prove the thing that is easy to get wrong and impossible to notice until someone's
 * game refuses to start: that a mod including <m2.h> and linking $(M2_LDFLAGS) produces a loadable
 * 32-bit .asi whose import table names m2-sdk.dll.
 *
 * It never runs here — CI cross-compiles it and inspects the PE. Compiling is not the interesting
 * part; the IMPORT TABLE is. A wrong M2_API direction, a missing export, or a name-mangling change
 * all still compile and only fail at load time on someone else's machine.
 */
#include "m2.h"

static void OnWorldLoaded(int reached_idx, void* ud) {
    (void)ud;
    m2_logf("world reached phase %d (%s)", reached_idx, m2_loadtrigger_phase_name(reached_idx));
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    /* Refuse to load against an m2-sdk.dll older than the header we compiled against, rather than
     * calling an export whose signature may have changed. The loader binds by name only. */
    if (!m2_abi_ok()) return FALSE;

    m2_log_init(inst);
    m2_logf("consumer smoke test, m2 %s", m2_version_string());

    m2_hook_init();
    m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, OnWorldLoaded, NULL);
    m2_loadtrigger_install();
    return TRUE;
}
