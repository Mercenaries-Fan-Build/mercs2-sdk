/* Compiled, never linked or run, by test/version/Makefile: the SDK version a mod compiles against.
 *
 * The Makefile passes the version it expects as M2_TEST_MAJOR/MINOR/PATCH. Every check is a
 * _Static_assert, so the compiler's exit code is the result.
 */
#include "m2_version.h"

_Static_assert(M2_VERSION_MAJOR == M2_TEST_MAJOR, "M2_VERSION_MAJOR is not the expected SDK version");
_Static_assert(M2_VERSION_MINOR == M2_TEST_MINOR, "M2_VERSION_MINOR is not the expected SDK version");
_Static_assert(M2_VERSION_PATCH == M2_TEST_PATCH, "M2_VERSION_PATCH is not the expected SDK version");
_Static_assert(M2_VERSION_NUM == M2_VERSION_AT_LEAST(M2_TEST_MAJOR, M2_TEST_MINOR, M2_TEST_PATCH),
               "M2_VERSION_NUM is not the expected SDK version");

/* m2_abi_ok() accepts a DLL whose MAJOR matches and whose number is at least M2_VERSION_NUM. At 0 it
 * accepts every 0.x DLL, whatever the mod was written against. */
_Static_assert(M2_VERSION_NUM > 0, "M2_VERSION_NUM is 0: m2_abi_ok() would accept every 0.x m2-sdk.dll");

int m2_test_guard(void) { return m2_abi_ok(); }
