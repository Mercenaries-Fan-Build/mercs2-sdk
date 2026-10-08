/* spawn_registry.asi — a thin loadable ASI that installs the SDK's spawn/guidmap
 * registry detour.
 *
 * All the hook logic lives in the shared layer (m2/m2_spawn_registry.c). This consumer
 * exists only to give the game something to LoadLibrary: it binds to m2-sdk.dll, guards
 * the ABI, and asks the SDK to install the detour. Built exactly the way a real mod is —
 * `include ../../sdk.mk`, link $(M2_LDFLAGS) — so its import table proves it binds to the
 * shared DLL rather than having inlined a private copy.
 */
#include "m2.h"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    /* Refuse to load against an older m2-sdk.dll than the header we compiled against. */
    if (!m2_abi_ok()) return FALSE;

    m2_log_init(inst);
    m2_logf("spawn_registry consumer: install requested, m2 %s", m2_version_string());

    m2_hook_init();
    m2_spawn_registry_install();   /* diagnostics land in m2-sdk.log next to the DLL */
    m2_worldentity_install();      /* worldentity template registry (observe_loader=1 => pure observe) */
    return TRUE;
}
