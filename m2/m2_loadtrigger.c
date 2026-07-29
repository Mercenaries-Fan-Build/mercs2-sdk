/* Load-phase triggers in a SHARED layer.
 *
 * Three things change once one instance serves N mods, and all three were silent failures:
 *
 *   1. **Late registration never fired.** Triggers only ran when the ladder ADVANCED past them, so a
 *      mod registering for "world loaded" after the world had loaded simply never ran. With the SDK
 *      compiled into each mod that was rare — everyone registered from their own DllMain, before
 *      anything loaded. Shared, the 2nd..Nth mod registering late is the NORMAL case. A trigger
 *      whose phase has already passed now fires immediately.
 *   2. **The registry was written unlocked**, `g_triggerCount++` from each mod's worker thread.
 *   3. **The log listener was registered once per caller.** N mods calling `m2_loadtrigger_install`
 *      meant N copies of `OnLogLine`, so every log line was substring-matched against the whole
 *      21-phase ladder N times. One instance now registers exactly one listener.
 */
#include "m2_loadtrigger.h"
#include "m2_loghook.h"
#include <windows.h>
#include <string.h>

#define MAX_TRIGGERS 32

typedef struct {
    int target_idx;
    m2_phase_cb volatile cb;   /* published last; NULL means "slot claimed, not ready" */
    void* ud;
    volatile LONG fired;
} Trigger;

static Trigger g_triggers[MAX_TRIGGERS];
static volatile LONG g_triggerCount = 0;
static volatile LONG g_maxPhase = -1;
static volatile LONG g_listenerAdded = 0;

/* Fire a trigger exactly once, whichever thread gets there first — the registering thread finding
 * the phase already passed, or the log thread advancing onto it. */
static void FireOnce(Trigger* t, int reached) {
    if (InterlockedCompareExchange(&t->fired, 1, 0) == 0) {
        m2_phase_cb cb = t->cb;
        if (cb) cb(reached, t->ud);
    }
}

int m2_loadtrigger_on_phase(int target_idx, m2_phase_cb cb, void* ud) {
    LONG slot;
    int reached;

    if (!cb) return 0;
    if (target_idx < 0 || target_idx >= M2_LADDER_COUNT) return 0;

    /* Claim a slot atomically. The count is never decremented on overflow: rolling it back would
     * corrupt a slot another thread had already claimed. */
    slot = InterlockedIncrement(&g_triggerCount) - 1;
    if (slot >= MAX_TRIGGERS) return 0;

    g_triggers[slot].target_idx = target_idx;
    g_triggers[slot].ud = ud;
    g_triggers[slot].fired = 0;
    /* Publish the callback LAST — a dispatcher that sees the slot before this is complete skips it. */
    InterlockedExchangePointer((void* volatile*)&g_triggers[slot].cb, (void*)cb);

    /* ★ Already past this phase? Fire now rather than never. */
    reached = (int)InterlockedCompareExchange(&g_maxPhase, -1, -1);
    if (reached >= target_idx) FireOnce(&g_triggers[slot], reached);
    return 1;
}

int m2_loadtrigger_current_phase(void) {
    return (int)InterlockedCompareExchange(&g_maxPhase, -1, -1);
}

const char* m2_loadtrigger_phase_name(int idx) {
    int i;
    for (i = 0; i < M2_LADDER_COUNT; i++)
        if (k_m2_ladder[i].idx == idx) return k_m2_ladder[i].name;
    return "?";
}

/* Highest ladder phase whose marker appears in `msg` (case-sensitive substring,
 * matching loadprobe's report.rs), or -1 if none. */
static int MatchPhase(const char* msg) {
    int best = -1, i, j;
    for (i = 0; i < M2_LADDER_COUNT; i++) {
        const M2LoadPhase* ph = &k_m2_ladder[i];
        for (j = 0; j < ph->match_count; j++) {
            if (strstr(msg, ph->matches[j])) {
                if (ph->idx > best) best = ph->idx;
                break;
            }
        }
    }
    return best;
}

static void OnLogLine(const char* msg, void* ud) {
    int matched, i;
    LONG n;
    (void)ud;

    matched = MatchPhase(msg);
    if (matched < 0) return;
    if (matched <= (int)InterlockedCompareExchange(&g_maxPhase, -1, -1)) return;  /* forward only */
    InterlockedExchange(&g_maxPhase, matched);

    n = InterlockedCompareExchange(&g_triggerCount, 0, 0);
    if (n > MAX_TRIGGERS) n = MAX_TRIGGERS;
    for (i = 0; i < (int)n; i++) {
        if (g_triggers[i].cb && g_triggers[i].target_idx <= matched)
            FireOnce(&g_triggers[i], matched);
    }
}

int m2_loadtrigger_install(void) {
    /* One listener for the whole process, however many mods ask. */
    if (InterlockedCompareExchange(&g_listenerAdded, 1, 0) == 0) {
        if (!m2_loghook_add_listener(OnLogLine, NULL)) {
            InterlockedExchange(&g_listenerAdded, 0);
            return 0;
        }
    }
    return m2_loghook_install();
}
