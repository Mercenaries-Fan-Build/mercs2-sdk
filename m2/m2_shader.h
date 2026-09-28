/* m2_shader.h — register new shaders in the game's own shader registry.
 *
 * The game builds its registry once, in FUN_0084f130, which the renderer constructor
 * (FUN_007492d0) calls during startup. A mod queues its shaders from its DllMain
 * (DLL_PROCESS_ATTACH), which runs before that, and the SDK registers every queued entry right
 * after the game's own shaders, through the same engine call retail uses (FUN_0085ac90):
 *
 *     static const m2_shader_class kGlow[4] = {
 *         { "MyGlowFP",       "MyGlowFP.sho" },        // class 0: always registered
 *         { "MyGlowFP_pl",    "MyGlowFP_pl.sho" },     // classes 1-3: registered when the
 *         { "MyGlowFP_sl",    "MyGlowFP_sl.sho" },     //   game's ShaderLevel byte (0x00dfc345)
 *         { "MyGlowFP_pl_sl", "MyGlowFP_pl_sl.sho" },  //   is non-zero, as retail does
 *     };
 *     static const m2_shader_class kGlowVs = { "MyGlowVP", "MyGlowVP.sho" };
 *
 *     if (m2_shader_add_pixel(M2_SHADER_FAMILY_BLUR_PIXEL, kGlow) != M2_SHADER_OK) ...
 *     if (m2_shader_add_vertex(M2_SHADER_FAMILY_VERTEX, &kGlowVs) != M2_SHADER_OK) ...
 *
 * Every function returns a status and none of them exits or aborts. The add functions report what
 * they can check at call time; what the game's registry decides (a name it already holds, a full
 * registry) is recorded per name and read back with m2_shader_outcome once the registry is built.
 * A failed entry also writes one line to the calling module's log (m2_log_init).
 *
 * Call the add functions from DllMain only. The name and sho strings are read when the registry
 * is built, after DllMain returns, so they must stay valid for the life of the process.
 */
#ifndef M2_SHADER_H
#define M2_SHADER_H

#include "m2_api.h"
#include "m2_shader_types.h"

/* Queue one pixel shader with its 4 classes (plain, _pl, _sl, _pl_sl), in that order. The 4 names
 * must differ from each other and from every queued name. `family` must be a pixel family.
 *
 * The first call checks the signatures of the engine functions involved, that the game has not
 * started its registry, and installs the registry detour: SIGNATURE, TOO_LATE and HOOK report
 * those. Then FAMILY, ARGUMENT, DUPLICATE and CAPACITY report the entry itself. */
M2_API m2_shader_status m2_shader_add_pixel(m2_shader_family family,
                                            const m2_shader_class classes[4]);

/* Queue one vertex shader. `family` must be a vertex family. Statuses as m2_shader_add_pixel. */
M2_API m2_shader_status m2_shader_add_vertex(m2_shader_family family, const m2_shader_class* cls);

/* The registration result for a queued name, once the game has built its registry: OK when the
 * name's entry registered, or the error that stopped it (DUPLICATE: the game's registry already
 * holds one of the entry's names; CAPACITY: the entry does not fit). ARGUMENT for a name that was
 * never queued, or whose entry the registry has not reached. All names of a pixel entry share the
 * entry's result. */
M2_API m2_shader_status m2_shader_outcome(const char* name);

/* The status enumerator's name, e.g. "M2_SHADER_ERR_DUPLICATE"; NULL for a value outside the
 * enum. */
M2_API const char* m2_shader_status_name(m2_shader_status s);

#endif /* M2_SHADER_H */
