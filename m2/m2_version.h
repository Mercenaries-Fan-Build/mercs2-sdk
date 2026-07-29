/* m2_version.h — the SDK's own version.
 *
 * The SDK had no version of any kind while it lived inside mercs2-qol-mods: no macro, no tag, no
 * ABI marker. That was survivable when there was exactly one consumer and it was vendored in the
 * same commit. It is not survivable now — the SDK is a shared dependency with independent
 * consumers, and "which m2 is this mod built against" has to be answerable.
 *
 * Semver over the SDK's own surface:
 *   MAJOR  a source-incompatible change to an m2_* signature or to sdk.mk's contract
 *   MINOR  new API, existing calls unchanged
 *   PATCH  fixes behind an unchanged API
 *
 * Consumers can gate on it:
 *
 *     #if M2_VERSION_NUM < M2_VERSION_AT_LEAST(0, 2, 0)
 *     #error this mod needs m2 >= 0.2.0
 *     #endif
 *
 * ⚠ This versions the SDK, NOT the game it targets. The build the hardcoded VAs in m2_target.h
 * belong to is a separate axis — see that header.
 */
#ifndef M2_VERSION_H
#define M2_VERSION_H

#include "m2_api.h"

#define M2_VERSION_MAJOR 0
#define M2_VERSION_MINOR 1
#define M2_VERSION_PATCH 0

#define M2_VERSION_AT_LEAST(maj, min, pat) ((maj) * 10000 + (min) * 100 + (pat))

/* Ordered and comparable: 0.1.0 -> 100. */
#define M2_VERSION_NUM \
    M2_VERSION_AT_LEAST(M2_VERSION_MAJOR, M2_VERSION_MINOR, M2_VERSION_PATCH)

#define M2_VERSION_STRING "0.1.0"

/* What the LOADED m2-sdk.dll reports, which may differ from the header this mod compiled against. */
M2_API int m2_version_num(void);
M2_API const char* m2_version_string(void);

/* Does the DLL we linked against satisfy the header we compiled against?
 *
 * The loader binds imports by NAME only, so a mod built against a newer header links happily to an
 * older DLL and then calls a function whose signature has changed — corrupting the stack with no
 * diagnostic. Check once in DllMain:
 *
 *     if (!m2_abi_ok()) return FALSE;   // refuse to load rather than misbehave
 *
 * A DLL NEWER than the header is fine (additive minor/patch). Older, or a different MAJOR, is not.
 */
static __inline int m2_abi_ok(void) {
    int have = m2_version_num();
    if (have / 10000 != M2_VERSION_MAJOR) return 0;
    return have >= M2_VERSION_NUM;
}

#endif /* M2_VERSION_H */
