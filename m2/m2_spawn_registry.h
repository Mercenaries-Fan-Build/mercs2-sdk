/* m2_spawn_registry.h — make a PATCH overlay's block-3185 additions actually register.
 *
 * The game's component-type-singleton get-or-create (FUN_004CC130 @ 0x004CC130) has a
 * bug: once a type's singleton slot is occupied, a second get-or-create for that type
 * returns the existing instance and DROPS its param_2 without ever calling the create.
 * For the guidmap and worldentity singletons that create is where a WAD overlay's new
 * spawn templates, name->handle bindings, and guidmap handles get merged into the global
 * registries. Dropped, they never register, so Pg.Spawn("landing_craft") stays
 * unresolvable.
 *
 * m2_spawn_registry_install() installs a MinHook detour (Option C — additive): on the
 * occupied-slot path for the gated types it force-calls the create for its additive
 * side-effects (new keys append into the global registries; base keys update in place)
 * while leaving the base's singleton INSTANCE in the slot, then returns that instance so
 * every existing caller sees exactly what it saw before.
 *
 * Defensive by construction:
 *   - a 16-byte prologue signature guard refuses to attach on a build variant that does
 *     not match, so it fails safe rather than detouring the wrong code;
 *   - a "spawn_registry.ini" next to m2-sdk.dll gates the whole hook (`enabled`) and each
 *     risky type independently (`hook_worldentity` / `hook_guidmap`);
 *   - install and every forced-create fire a line into m2-sdk.log.
 *
 * Call once, from a mod's DllMain, after m2_hook_init(). Diagnostics land in
 * "<m2-sdk.dll dir>\m2-sdk.log" and config is read from "<same dir>\spawn_registry.ini".
 */
#ifndef M2_SPAWN_REGISTRY_H
#define M2_SPAWN_REGISTRY_H

#include "m2_api.h"

/* Read spawn_registry.ini, verify the hook site's prologue, and (if enabled + matching)
 * install the detour. Idempotent-safe to the extent m2_hook_attach is; logs its outcome.
 * Never throws and never attaches on a signature mismatch. */
M2_API void m2_spawn_registry_install(void);

#endif /* M2_SPAWN_REGISTRY_H */
