/* Per-module logging in a SHARED layer.
 *
 * This used to be one `static HANDLE g_logFile`, which was correct while the SDK was compiled into
 * each mod: one copy, one handle, one mod. In a single shared DLL that collapses — mod B's
 * `m2_log_init` would overwrite mod A's handle (leaking it) and every subsequent line from any mod
 * would land in one file.
 *
 * So the handle is keyed by the CALLER'S HMODULE. The public API did not have to change to make
 * that possible: `m2_log_init` already took the module, and `m2_logf` is a header macro that passes
 * `M2_SELF_MODULE` (the linker's `__ImageBase`), so call sites stay exactly as they were.
 */
#include "m2_log.h"
#include <string.h>
#include <stdarg.h>

/* Small fixed table rather than a growing map: the process holds a handful of mods, an entry is 8
 * bytes, and a fixed array needs no allocator on a path that runs during DllMain. */
#define M2_LOG_MAX_MODULES 32

typedef struct {
    HMODULE module;
    HANDLE  file;
} LogSlot;

static LogSlot g_slots[M2_LOG_MAX_MODULES];
static int g_slotCount = 0;
static CRITICAL_SECTION g_lock;
static volatile LONG g_lockReady = 0;

/* Callers arrive from their own DllMain and from worker threads, so the table needs a lock and the
 * lock needs to exist before the first caller. One-shot init via an interlocked latch, with the
 * loser spinning until the winner has finished initialising. */
static void EnsureLock(void) {
    if (InterlockedCompareExchange(&g_lockReady, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_lockReady, 2);
        return;
    }
    while (InterlockedCompareExchange(&g_lockReady, 2, 2) != 2) {
        Sleep(0);
    }
}

static HANDLE FileFor(HMODULE module) {
    int i;
    HANDLE h = INVALID_HANDLE_VALUE;
    EnsureLock();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < g_slotCount; i++) {
        if (g_slots[i].module == module) {
            h = g_slots[i].file;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    return h;
}

void m2_module_path(HMODULE module, const char* filename, char* out, int out_size) {
    char dir[MAX_PATH];
    char* slash;
    GetModuleFileNameA(module, dir, MAX_PATH);
    slash = strrchr(dir, '\\');
    if (!slash) slash = strrchr(dir, '/');
    if (slash) *(slash + 1) = '\0'; else dir[0] = '\0';
    lstrcpynA(out, dir, out_size);
    if ((int)strlen(out) + (int)strlen(filename) < out_size)
        strcat(out, filename);
}

void m2_log_init(HMODULE module) {
    char path[MAX_PATH];
    char* dot;
    HANDLE file;
    int i;

    if (FileFor(module) != INVALID_HANDLE_VALUE) return;  /* already open for this module */

    GetModuleFileNameA(module, path, MAX_PATH);
    dot = strrchr(path, '.');
    if (dot) strcpy(dot, ".log");
    else strcat(path, ".log");
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    EnsureLock();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < g_slotCount; i++) {
        if (g_slots[i].module == module) break;   /* lost a race; keep the winner's handle */
    }
    if (i < g_slotCount) {
        LeaveCriticalSection(&g_lock);
        CloseHandle(file);
        return;
    }
    if (g_slotCount < M2_LOG_MAX_MODULES) {
        g_slots[g_slotCount].module = module;
        g_slots[g_slotCount].file = file;
        g_slotCount++;
        file = INVALID_HANDLE_VALUE;               /* the table owns it now */
    }
    LeaveCriticalSection(&g_lock);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);  /* table full — do not leak */
}

void m2_logf_mod(HMODULE module, const char* fmt, ...) {
    char buf[1536];
    int len;
    va_list ap;
    DWORD written;
    HANDLE file = FileFor(module);
    if (file == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    len = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (len <= 0) return;
    if (len > (int)sizeof(buf) - 2) len = (int)sizeof(buf) - 2;
    buf[len] = '\r';
    buf[len + 1] = '\n';
    WriteFile(file, buf, len + 2, &written, NULL);
    FlushFileBuffers(file);
}

void m2_log_close_mod(HMODULE module) {
    int i;
    HANDLE file = INVALID_HANDLE_VALUE;
    EnsureLock();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < g_slotCount; i++) {
        if (g_slots[i].module == module) {
            file = g_slots[i].file;
            g_slots[i] = g_slots[g_slotCount - 1];   /* compact; order is not meaningful */
            g_slotCount--;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}
