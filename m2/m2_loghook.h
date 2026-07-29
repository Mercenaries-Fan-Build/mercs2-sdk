/* m2_loghook.h — shared subscription to the game's log stream.
 *
 * The SDK's single event source for "what did the game just say"; m2_loadtrigger
 * is built on it and mods can listen directly.
 *
 * Source selection (see m2_loghook_install), best first:
 *   - pmc_bb >= 3.1.0 exports pmc_log_subscribe — we subscribe to its live
 *     in-process log stream. No file, and it works in pmc_bb's default
 *     markers-only mode, so PMC_VERBOSE_LOG is not required.
 *   - Older pmc_bb: it owns the log-stub hook and writes every line to
 *     pmc_blackbox.log — we TAIL that file (pure consumer; we never touch the
 *     stub, so we can't shadow pmc_bb's logger).
 *   - No pmc_bb: we MinHook the shared no-op log stub (M2_LOG_STUB_VA) ourselves,
 *     chaining the trampoline. Every Lua print / Debug.Printf / stripped subsystem
 *     log line funnels through it; string args are joined into a message.
 */
#ifndef M2_LOGHOOK_H
#define M2_LOGHOOK_H

#include "m2_api.h"

/* Called on the game thread for each captured log line (NUL-terminated message,
 * string args tab-joined). Keep it cheap and non-reentrant — do not call back
 * into anything that itself logs. */
typedef void (*m2_log_listener)(const char* msg, void* ud);

/* Register a listener. Returns 1 on success, 0 if the table is full. Register
 * before m2_loghook_install(). */
M2_API int m2_loghook_add_listener(m2_log_listener cb, void* ud);

/* MinHook the log stub and begin dispatching. Idempotent. Returns 1 on success. */
M2_API int m2_loghook_install(void);

#endif /* M2_LOGHOOK_H */
