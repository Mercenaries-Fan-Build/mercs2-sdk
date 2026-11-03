/* m2.h — umbrella include for the Mercenaries 2 mod stdlib.
 *
 * A small reusable runtime layer for Mercs2 ASI mods: per-module logging, INI
 * config, SecuROM-safe MinHook detours, safe Lua-stack reads, a shared subscription
 * to the game's log stream, and load-progress triggers keyed to loadprobe's
 * world-load ladder, registration of new shaders in the game's shader
 * registry, and the render sites a fix detours, resolved in the running build.
 * Link against m2-sdk.dll via sdk.mk.
 */
#ifndef M2_H
#define M2_H

#include "m2_version.h"
#include "m2_target.h"
#include "m2_log.h"
#include "m2_ini.h"
#include "m2_hook.h"
#include "m2_luastack.h"
#include "m2_loghook.h"
#include "m2_loadtrigger.h"
#include "m2_shader.h"
#include "m2_lua.h"
#include "m2_render.h"

#endif /* M2_H */
