/* m2_worldentity.h — own the immutable resident worldentity; expose a MUTABLE PARALLEL.
 *
 * The base game's resident worldentity singleton (the spawn-template set) is immutable to us: a WAD
 * override is inert (it double-registers the singleton → render-view crash), and it is created once
 * by the asset system. This module makes that immutable set EXTENSIBLE: mods register novel spawn
 * templates as DATA, and at world-load the module drives the game's OWN COMP loader (`FUN_00654940`)
 * over an authored additive block so every registered template becomes a live, `Pg.Spawn`-able entity
 * — the engine mints the dense index, inserts the reflection records + signature, and instantiates
 * the subgraph itself. No base bytes are changed; the singleton is registered exactly once, so the
 * double-registration fault cannot occur.
 *
 * This is the general abstraction; a novel prop, a vehicle, any new entity is just a consumer that
 * registers template DATA. N mods compose — all their templates inject in one load pass.
 *
 * Usage from a consumer's DllMain:
 *     if (!m2_abi_ok()) return FALSE;
 *     static const unsigned model = 0xC0FFEE00u;   // the entity's mesh asset hash
 *     m2_comp_rec_t comps[] = { { 0x5CF81991u, &model, 4 }, ... };  // ModelName + more
 *     m2_worldentity_register_template("my_template", comps, N);
 *     m2_worldentity_install();
 */
#ifndef M2_WORLDENTITY_H
#define M2_WORLDENTITY_H

#include "m2_api.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One authored reflection-component record: the on-disk COMP class hash + its payload bytes (the
 * same bytes a retail template of this kind carries). Both `payload` and the enclosing array are
 * stored BY REFERENCE and must outlive the process — use static/const storage. */
typedef struct {
    unsigned    class_hash;   /* e.g. ModelName 0x5CF81991, _PropPhysics 0xB03943A2 */
    const void* payload;
    unsigned    payload_len;
} m2_comp_rec_t;

/* Register a novel spawn template to be made live in the worldentity at world-load.
 *   name  — the Pg.Spawn name (stored BY REFERENCE; must outlive the process).
 *   comps — the component set (by reference; may be NULL / n_comps 0 for a name-only template).
 * Returns 1 if queued, 0 on failure (registry full / bad args). Call from your DllMain, then call
 * m2_worldentity_install(). Multiple mods may register; every template injects in one load pass. */
M2_API int m2_worldentity_register_template(const char* name,
                                            const m2_comp_rec_t* comps, size_t n_comps);

/* Arm the world-load injection. Idempotent — safe to call from every consumer's DllMain. */
M2_API void m2_worldentity_install(void);

#ifdef __cplusplus
}
#endif

#endif /* M2_WORLDENTITY_H */
