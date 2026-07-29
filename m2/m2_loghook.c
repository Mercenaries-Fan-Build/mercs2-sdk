#include "m2_loghook.h"
#include "m2_hook.h"
#include "m2_luastack.h"
#include "m2_target.h"
#include <windows.h>
#include <string.h>

#define MAX_LISTENERS 16

/* `cb` is published last and read first, so a dispatcher can tell a fully-written slot from one
 * another thread has only just claimed. */
typedef struct { m2_log_listener volatile cb; void* ud; } Listener;

typedef int(__cdecl *LogStubFn)(void* L);

static Listener g_listeners[MAX_LISTENERS];
static volatile LONG g_listenerCount = 0;
static volatile LONG g_installed = 0;
static volatile LONG g_inHook = 0;
static void* g_origStub = NULL;  /* MinHook trampoline — chained, see Hook_LogStub */

/* Lock-free rather than locked, deliberately: this can be called from a mod's DllMain, which runs
 * under the Windows loader lock. Taking a CRITICAL_SECTION there that another thread holds while
 * itself waiting on the loader lock is a textbook deadlock. An interlocked slot claim cannot block.
 *
 * Registering AFTER m2_loghook_install() is fine, and is now the normal case for the 2nd..Nth mod:
 * the hook is already live and simply starts dispatching to the new listener on the next line.
 */
int m2_loghook_add_listener(m2_log_listener cb, void* ud) {
    LONG slot;
    if (!cb) return 0;
    /* Never rolled back on overflow: decrementing would hand a slot another thread already owns
     * back out a second time. */
    slot = InterlockedIncrement(&g_listenerCount) - 1;
    if (slot >= MAX_LISTENERS) return 0;
    g_listeners[slot].ud = ud;
    InterlockedExchangePointer((void* volatile*)&g_listeners[slot].cb, (void*)cb);
    return 1;
}

static void DispatchLine(const char* msg) {
    LONG n = InterlockedCompareExchange(&g_listenerCount, 0, 0);
    int i;
    if (n > MAX_LISTENERS) n = MAX_LISTENERS;
    for (i = 0; i < (int)n; i++) {
        m2_log_listener cb = g_listeners[i].cb;
        if (cb) cb(msg, g_listeners[i].ud);   /* NULL = claimed but not yet published */
    }
}

/* --- Preferred path: tail the log pmc_bb already writes ---
 *
 * When pmc_bb.dll is loaded it owns the stub hook and records every line to
 * pmc_blackbox.log next to the game exe. Rather than hook the same 5 bytes (and
 * risk shadowing it), we just follow that file. pmc_bb stays the sole hook
 * owner; we're a pure consumer; load order is irrelevant. */

#define M2_TAIL_POLL_MS 300
#define M2_TAIL_WAIT_MS 20000   /* wait this long for the log to appear */
#define M2_TAIL_LINE_MAX 2048

static void BuildLogPath(char* out, int n) {
    char exe[MAX_PATH];
    char* slash;
    GetModuleFileNameA(NULL, exe, MAX_PATH);   /* the game exe */
    slash = strrchr(exe, '\\');
    if (!slash) slash = strrchr(exe, '/');
    if (slash) *(slash + 1) = '\0'; else exe[0] = '\0';
    lstrcpynA(out, exe, n);
    if ((int)strlen(out) + 17 < n) strcat(out, "pmc_blackbox.log");
}

static DWORD WINAPI TailThread(LPVOID param) {
    char path[MAX_PATH];
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD waited = 0;
    LARGE_INTEGER off;
    char line[M2_TAIL_LINE_MAX];
    int linelen = 0;
    (void)param;

    BuildLogPath(path, (int)sizeof(path));

    /* pmc_bb truncates+opens the log at its own startup; wait for it. */
    while (waited < M2_TAIL_WAIT_MS) {
        h = CreateFileA(path, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) break;
        Sleep(M2_TAIL_POLL_MS);
        waited += M2_TAIL_POLL_MS;
    }
    if (h == INVALID_HANDLE_VALUE) return 0;

    off.QuadPart = 0;   /* read from the start so we don't miss early markers */
    for (;;) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(h, &size)) {
            if (size.QuadPart < off.QuadPart) { off.QuadPart = 0; linelen = 0; } /* re-truncated */
            if (size.QuadPart > off.QuadPart) {
                char buf[1024];
                DWORD got;
                SetFilePointerEx(h, off, NULL, FILE_BEGIN);
                while (ReadFile(h, buf, sizeof(buf), &got, NULL) && got > 0) {
                    DWORD k;
                    off.QuadPart += got;
                    for (k = 0; k < got; k++) {
                        char c = buf[k];
                        if (c == '\n' || c == '\r') {
                            if (linelen > 0) {
                                line[linelen] = '\0';
                                DispatchLine(line);
                                linelen = 0;
                            }
                        } else if (linelen < M2_TAIL_LINE_MAX - 1) {
                            line[linelen++] = c;
                        }
                    }
                    if (got < sizeof(buf)) break;
                }
            }
        }
        Sleep(M2_TAIL_POLL_MS);
    }
}

/* The detour. The stub is `xor eax,eax; ret`, reached by ~700 callers; only Lua
 * print/Debug.Printf pass a real lua_State*. m2_lua_join_strings rejects the rest
 * (returns -1), so non-Lua callers cost almost nothing.
 *
 * We observe the line for our listeners, then ALWAYS chain to the trampoline.
 * Chaining is essential: pmc_bb.dll hooks this same shared stub for its own
 * logging, and it installs first (at boot) — so the bytes MinHook captured into
 * our trampoline are pmc_bb's jump, not the bare stub. If we returned without
 * calling it we'd silently shadow pmc_bb's logger and pmc_blackbox.log would stop
 * populating. The re-entrancy guard wraps only our listener dispatch. */
static int __cdecl Hook_LogStub(void* L) {
    char msg[2048];

    if (InterlockedCompareExchange(&g_inHook, 1, 0) == 0) {
        if (m2_lua_join_strings(L, msg, (int)sizeof(msg)) >= 1) DispatchLine(msg);
        InterlockedExchange(&g_inHook, 0);
    }

    if (g_origStub) return ((LogStubFn)g_origStub)(L);
    return 0;
}

/* --- Best path (pmc_bb >= 3.1.0): subscribe to its live in-process log stream ---
 *
 * pmc_bb exports pmc_log_subscribe(cb, ud): cb fires for every log line as it's
 * written — no file tail, and it works in pmc_bb's default markers-only mode, so
 * PMC_VERBOSE_LOG is NOT required for load-progress visibility. The sink shape
 * matches m2_log_listener. pmc_bb hands us the fully-formatted line WITH a
 * trailing newline; strip it so listeners see the same shape as the tail path.
 * The callback runs under pmc_bb's log lock — stays cheap, never calls pmc_log. */
typedef int (*pmc_log_subscribe_fn)(m2_log_listener cb, void* ud);

static void OnPmcLine(const char* line, void* ud) {
    char buf[2048];
    int n;
    (void)ud;
    if (!line) return;
    n = (int)strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    memcpy(buf, line, (size_t)n);
    buf[n] = '\0';
    DispatchLine(buf);
}

int m2_loghook_install(void) {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return 1;

    /* Best: subscribe to pmc_bb's live log stream (pmc_bb >= 3.1.0). In-process,
     * no file, works in pmc_bb's default markers-only mode (no PMC_VERBOSE_LOG). */
    {
        HMODULE bb = GetModuleHandleA("pmc_bb.dll");
        if (bb) {
            pmc_log_subscribe_fn sub =
                (pmc_log_subscribe_fn)GetProcAddress(bb, "pmc_log_subscribe");
            if (sub && sub(OnPmcLine, NULL)) return 1;
            /* older pmc_bb without the export — fall through to the file tail */
        }
    }

    /* Fallback (older pmc_bb): tail the log it already writes — pure consumer,
     * we never touch the stub, so we can't shadow pmc_bb's logger. */
    if (GetModuleHandleA("pmc_bb.dll")) {
        if (CreateThread(NULL, 0, TailThread, NULL, 0, NULL)) return 1;
        /* thread spawn failed — fall through to self-hook */
    }

    /* No pmc_bb at all: hook the shared stub ourselves (chained). */
    if (!m2_hook_attach((void*)M2_LOG_STUB_VA, (void*)Hook_LogStub, &g_origStub)) {
        InterlockedExchange(&g_installed, 0);
        return 0;
    }
    return 1;
}
