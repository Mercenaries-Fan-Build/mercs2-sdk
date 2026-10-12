/* Exercises every m2-sdk.dll export that runs outside the game and prints what each returns, so two
 * builds of the DLL can be compared byte for byte (`make compare BASELINE_DLL=...`). Each call's output
 * goes to stdout, and the m2_logf lines to runtime.log next to the exe; both are compared.
 *
 * Not exercised here: m2_shader_add_* (they read the game's code at fixed addresses) and the log-stub
 * detour of m2_loghook_install (the stub address is not mapped outside the game).
 */
#include <stdio.h>
#include <string.h>
#include "m2.h"

static void OnKey(void* ud, const char* key, const char* value) {
    int* n = (int*)ud;
    printf("ini[%d] key=\"%s\" value=\"%s\" bool=%d int=%d\n", (*n)++, key, value, m2_ini_bool(value),
           m2_ini_int(value, -7));
}

static int g_calls = 0;
static int (*g_orig)(int) = NULL;

__attribute__((noinline)) int HookTarget(int x) {
    volatile int a = x;
    volatile int b = a * 3;
    return b + 1;
}

static int HookDetour(int x) {
    g_calls++;
    return g_orig(x) + 1000;
}

static void OnPhase(int reached, void* ud) {
    printf("phase callback reached=%d ud=%p\n", reached, ud);
}

/* A Lua 5.1 float-build state: TValue {u32 value; u32 tt}, top +0x08, base +0x0C, ci +0x14,
 * stack_last +0x1C, stack +0x20, TString bytes at +16. */
typedef struct { unsigned value, tt; } TV;
static unsigned char g_L[0x48] __attribute__((aligned(4)));
static TV g_stack[40];
static unsigned char g_str1[32] __attribute__((aligned(4)));
static unsigned char g_str2[32] __attribute__((aligned(4)));

static void Put(unsigned off, const void* v) { memcpy(g_L + off, &v, 4); }

static void* FakeState(int nargs, int base_ok) {
    memset(g_L, 0, sizeof(g_L));
    Put(0x08, &g_stack[1 + nargs]);
    Put(0x0C, base_ok ? (void*)&g_stack[1] : NULL);
    Put(0x14, g_L + 0x40);
    Put(0x1C, &g_stack[39]);
    Put(0x20, &g_stack[0]);
    Put(0x40, &g_stack[1]);
    Put(0x44, &g_stack[0]);
    return g_L;
}

int main(void) {
    char buf[600];
    char path[MAX_PATH];
    int i, n;
    double d;
    FILE* f;

    m2_log_init(GetModuleHandleA(NULL));
    m2_log_init(GetModuleHandleA(NULL));
    m2_logf("int %d neg %d str %s char %c hex %08X lower %x unsigned %u", 42, -5, "abc", 'Z', 0xBEEFu, 255u, 4000000000u);
    m2_logf("width [%5d] [%-5d] [%05d] [%.3s] [%10s] [%-10s]", 7, 7, 7, "abcdef", "right", "left");
    m2_logf("percent %% and a long %ls", L"wide");
    memset(buf, 'x', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m2_logf("long %s %s", buf, buf);
    m2_logf("");
    m2_logf("after empty");

    m2_module_path(NULL, "lua_bridge.ini", path, sizeof(path));
    printf("module_path tail=\"%s\"\n", strrchr(path, '\\') ? strrchr(path, '\\') : path);
    m2_module_path(NULL, "name.ini", path, 6);
    printf("module_path small=\"%s\"\n", path);

    f = fopen("runtime_test.ini", "wb");
    fputs("; comment\r\n[section]\r\n  host = 127.0.0.1  \r\nport=27050 ; trailing\r\n\tflag\t=\tON # hash\r\n"
          "noequals\r\n=empty key\r\nk=\r\nneg=-12abc\r\nx=1=2\r\n", f);
    fclose(f);
    n = 0;
    printf("ini_parse=%d\n", m2_ini_parse("runtime_test.ini", OnKey, &n));
    printf("ini_parse missing=%d\n", m2_ini_parse("no_such_file.ini", OnKey, &n));
    f = fopen("runtime_empty.ini", "wb");
    fclose(f);
    printf("ini_parse empty=%d\n", m2_ini_parse("runtime_empty.ini", OnKey, &n));
    printf("ini_bool yes=%d YES=%d 2=%d off=%d\n", m2_ini_bool("yes"), m2_ini_bool("YES"), m2_ini_bool("2"),
           m2_ini_bool("off"));
    printf("ini_int 99=%d -0=%d +1=%d empty=%d\n", m2_ini_int("99", 1), m2_ini_int("-0", 1), m2_ini_int("+1", 1),
           m2_ini_int("", 1));

    printf("hook_init=%d %d\n", m2_hook_init(), m2_hook_init());
    printf("before hook %d\n", HookTarget(2));
    n = m2_hook_attach((void*)HookTarget, (void*)HookDetour, (void**)&g_orig);
    printf("hook_attach=%d orig_set=%d\n", n, g_orig != NULL);
    n = HookTarget(2);
    printf("after hook %d calls=%d\n", n, g_calls);
    printf("hook_attach again=%d\n", m2_hook_attach((void*)HookTarget, (void*)HookDetour, NULL));

    for (i = -1; i <= 21; i++) printf("phase_name(%d)=\"%s\"\n", i, m2_loadtrigger_phase_name(i));
    printf("current_phase=%d\n", m2_loadtrigger_current_phase());
    printf("on_phase null=%d bad=%d over=%d ok=%d\n", m2_loadtrigger_on_phase(5, NULL, NULL),
           m2_loadtrigger_on_phase(-1, OnPhase, NULL), m2_loadtrigger_on_phase(21, OnPhase, NULL),
           m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, OnPhase, (void*)0x1234));
    printf("loghook_add null=%d\n", m2_loghook_add_listener(NULL, NULL));

    for (i = -1; i <= 8; i++) {
        const char* s = m2_shader_status_name((m2_shader_status)i);
        printf("status_name(%d)=%s\n", i, s ? s : "(null)");
    }
    printf("outcome(null)=%d outcome(\"\")=%d outcome(never)=%d\n", m2_shader_outcome(NULL), m2_shader_outcome(""),
           m2_shader_outcome("NeverQueued"));

    memcpy(g_str1 + 16, "hello", 6);
    memcpy(g_str2 + 16, "world", 6);
    g_stack[1].value = (unsigned)(size_t)g_str1; g_stack[1].tt = 4;
    { float fl = 1.5f; memcpy(&g_stack[2].value, &fl, 4); g_stack[2].tt = 3; }
    g_stack[3].value = 0; g_stack[3].tt = 1;
    g_stack[4].value = 0; g_stack[4].tt = 0;
    g_stack[5].value = (unsigned)(size_t)g_str2; g_stack[5].tt = 4;
    g_stack[6].value = 7; g_stack[6].tt = 5;
    {
        void* L = FakeState(6, 1);
        int b = -9;
        printf("nargs=%d\n", m2_lua_nargs(L));
        for (i = -1; i <= 6; i++) printf("arg_type(%d)=%d\n", i, m2_lua_arg_type(L, i));
        d = -1;
        n = m2_lua_arg_number(L, 1, &d);
        printf("arg_number(1)=%d %.3f\n", n, d);
        printf("arg_number(0)=%d\n", m2_lua_arg_number(L, 0, &d));
        printf("arg_number(1,null)=%d\n", m2_lua_arg_number(L, 1, NULL));
        for (i = 0; i <= 6; i++) {
            b = -9;
            n = m2_lua_arg_bool(L, i, &b);
            printf("arg_bool(%d)=%d %d\n", i, n, b);
        }
        memset(buf, 0x55, sizeof(buf));
        n = m2_lua_arg_string(L, 0, buf, 4);
        printf("arg_string(0,4)=%d \"%s\"\n", n, buf);
        n = m2_lua_arg_string(L, 1, buf, 16);
        printf("arg_string(1)=%d \"%s\"\n", n, buf);
        n = m2_lua_arg_string(L, 0, buf, 0);
        printf("arg_string(0,0)=%d\n", n);
        n = m2_lua_join_strings(L, buf, 64);
        printf("join=%d \"%s\"\n", n, buf);
        n = m2_lua_join_strings(L, buf, 8);
        printf("join(8)=%d \"%s\"\n", n, buf);
        L = FakeState(1, 0);
        printf("nargs ci fallback=%d\n", m2_lua_nargs(L));
        L = FakeState(0, 1);
        printf("nargs zero=%d join=%d\n", m2_lua_nargs(L), m2_lua_join_strings(L, buf, 8));
        printf("nargs bad=%d %d %d\n", m2_lua_nargs(NULL), m2_lua_nargs((void*)0x1234), m2_lua_nargs(g_L + 1));
    }

    m2_logf("closing");
    m2_log_close();
    m2_logf("after close is dropped");
    printf("done\n");
    return 0;
}
