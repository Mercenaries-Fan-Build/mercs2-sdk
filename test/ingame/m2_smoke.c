/* An in-game smoke test of m2-sdk.dll: install m2_smoke.asi in the game's scripts/ folder beside
 * m2-sdk.dll, load into the world, and read m2_smoke.log next to it. Each check writes one line, and
 * every step is logged before it runs, so a crash shows where it stopped.
 *
 * Checks: the version and per-module log; m2_module_path; the shader registry (two new entries that
 * load retail .sho files, which must register, and two that reuse a retail shader name, which the
 * registry must refuse as DUPLICATE); a log-stream listener, paused once the world is loaded; a load trigger on the world being fully
 * loaded, which reads back every shader outcome; the Lua host (a namespace, chunks run on the first
 * captured state, function references kept from Lua and called from C, the tick).
 */
#include "m2.h"

/* New names over retail .sho files: PgBlurHFP.sho is a retail pixel shader, PgMeshVP.sho a retail
 * vertex shader (FUN_0084f130 registers both). */
static const m2_shader_class kNewPixel[4] = {
    { "M2SmokeFP", "PgBlurHFP.sho" },
    { "M2SmokeFP_pl", "PgBlurHFP.sho" },
    { "M2SmokeFP_sl", "PgBlurHFP.sho" },
    { "M2SmokeFP_pl_sl", "PgBlurHFP.sho" },
};
static const m2_shader_class kNewVertex = { "M2SmokeVP", "PgMeshVP.sho" };

/* Class 0 reuses a retail name, so the registry pass refuses the entry before any engine call. */
static const m2_shader_class kDupPixel[4] = {
    { "PgBlurHFP", "PgBlurHFP.sho" },
    { "M2SmokeDupFP_pl", "PgBlurHFP.sho" },
    { "M2SmokeDupFP_sl", "PgBlurHFP.sho" },
    { "M2SmokeDupFP_pl_sl", "PgBlurHFP.sho" },
};
static const m2_shader_class kDupVertex = { "PgMeshVP", "PgMeshVP.sho" };

static volatile LONG g_lines = 0;
static volatile LONG g_ticks = 0;
static volatile LONG g_ran = 0;
static int g_failures = 0;

static void Check(int ok, const char* what) {
    if (!ok) g_failures++;
    m2_logf("%s: %s", ok ? "PASS" : "FAIL", what);
}

/* `text` holds `part`. */
static int Contains(const char* text, const char* part) {
    int n = lstrlenA(part);
    for (; *text; text++) {
        int i = 0;
        while (i < n && text[i] == part[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

static void Outcome(const char* name, m2_shader_status want) {
    m2_shader_status got = m2_shader_outcome(name);
    char line[160];
    wsprintfA(line, "outcome(%s) = %s, expected %s", name, m2_shader_status_name(got),
              m2_shader_status_name(want));
    Check(got == want, line);
}

static void OnLine(const char* msg, void* ud) {
    (void)msg;
    (void)ud;
    InterlockedIncrement(&g_lines);
}

static int SmokeHello(void* L) {
    m2_lua_push_string(L, "hi", -1);
    m2_lua_push_number(L, 2.5);
    return 2;
}

static int SmokeFail(void* L) {
    m2_lua_raise(L, "smoke failure");
    return 0;
}

/* M2Smoke.Keep(v): keeps a ref to v (m2_lua_ref of the first argument) and returns it, 0 for none. */
static int g_refs[4];
static int g_nrefs = 0;

static int SmokeKeep(void* L) {
    int r = m2_lua_ref(L, 1);
    if (r > 0 && g_nrefs < 4) g_refs[g_nrefs++] = r;
    m2_lua_push_number(L, r);
    return 1;
}

static const m2_lua_reg kSmokeLib[] = {
    { "Hello", SmokeHello },
    { "Fail", SmokeFail },
    { "Keep", SmokeKeep },
    { NULL, NULL },
};

static void Exec(void* L, const char* chunk, int want_status, const char* want_out) {
    char out[512];
    char line[768];
    int st = m2_lua_exec(L, chunk, lstrlenA(chunk), out, sizeof(out));
    wsprintfA(line, "m2_lua_exec(%s) = %d \"%s\", expected %d \"%s\"", chunk, st, out, want_status, want_out);
    Check(st == want_status && lstrcmpA(out, want_out) == 0, line);
}

static void Refs(void* L);

static void OnState(void* L, int is_new_cycle, void* ud) {
    (void)ud;
    m2_logf("state captured: L=%p new=%d", L, is_new_cycle);
    if (InterlockedCompareExchange(&g_ran, 1, 0) != 0) return;
    Exec(L, "return M2Smoke.Hello() == \"hi\", type(1), 1 + 1", 0, "[ok]\ttrue\t\"number\"\t2");
    Exec(L, "return select(2, M2Smoke.Hello())", 0, "[ok]\t2.5");
    Exec(L, "local ok, e = pcall(M2Smoke.Fail) return ok, e", 0, "[ok]\tfalse\t\"smoke failure\"");
    Exec(L, "return (", 1, "[compile] \"[string \"return (\"]:1: unexpected symbol near '<eof>'\"");
    Exec(L, "error(\"boom\", 0)", 2, "[runtime] \"boom\"");
    Check(!m2_lua_in_exec(), "m2_lua_in_exec is 0 outside a chunk");
    Refs(L);
}

/* Function references: keep two callbacks from Lua, call them from C, release them. */
static void Refs(void* L) {
    int depth = m2_lua_nargs(L);
    int st;
    const char* e;
    Exec(L, "return M2Smoke.Keep(function(x) M2SmokeGot = x * 2 end) > 0", 0, "[ok]\ttrue");
    Exec(L, "return M2Smoke.Keep(function() error(\"smoke callback\", 0) end) > 0", 0, "[ok]\ttrue");
    Exec(L, "return M2Smoke.Keep(nil)", 0, "[ok]\t0");
    Check(g_nrefs == 2, "m2_lua_ref kept the two functions and refused the nil");
    if (g_nrefs != 2) return;
    Check(g_refs[0] != g_refs[1], "the two refs differ");

    Check(m2_lua_push_ref(L, g_refs[0]) == 1, "m2_lua_push_ref of the kept callback returns 1");
    m2_lua_push_number(L, 21);
    st = m2_lua_pcall(L, 1, 0);
    Check(st == 0, "m2_lua_pcall of the kept callback returns 0");
    Exec(L, "return M2SmokeGot", 0, "[ok]\t42");

    Check(m2_lua_push_ref(L, g_refs[1]) == 1, "m2_lua_push_ref of the erroring callback returns 1");
    st = m2_lua_pcall(L, 0, 0);
    e = m2_lua_last_error();
    Check(st == 2, "m2_lua_pcall of the erroring callback returns 2 (LUA_ERRRUN)");
    Check(e != NULL && Contains(e, "smoke callback"), "its error is kept for m2_lua_last_error");
    if (e) m2_logf("callback error: %s", e);

    Check(m2_lua_pcall(L, 64, 0) == -1, "m2_lua_pcall refuses more arguments than the frame holds");
    Check(m2_lua_push_ref(L, g_refs[0]) == 1, "m2_lua_push_ref pushes the callback again");
    Check(m2_lua_pcall(L, 0, 1000) == -1, "m2_lua_pcall refuses 1000 results, more than checkresults allows");
    Check(m2_lua_pcall(L, 0, -2) == -1, "m2_lua_pcall refuses nresults below LUA_MULTRET");
    Check(m2_lua_nargs(L) == depth + 1, "the refused calls left the pushed callback in place");
    m2_lua_push_number(L, 4);
    Check(m2_lua_pcall(L, 1, 0) == 0, "m2_lua_pcall then calls it");
    Exec(L, "return M2SmokeGot", 0, "[ok]\t8");
    Check(m2_lua_nargs(L) == depth, "the frame is as it was");

    Check(m2_lua_unref(L, g_refs[0]) == 1, "m2_lua_unref releases the first ref");
    Check(m2_lua_unref(L, g_refs[1]) == 1, "m2_lua_unref releases the second ref");
    Check(m2_lua_nargs(L) == depth, "m2_lua_unref leaves the frame as it was");
    Check(m2_lua_unref(L, g_refs[0]) == 0, "a second m2_lua_unref of one ref is refused");
    Check(m2_lua_push_ref(L, g_refs[0]) == 0, "m2_lua_push_ref of a released ref is refused");
    Check(m2_lua_push_ref(L, 0x7fffffff) == 0, "a ref m2_lua_ref did not return is refused");
    Check(m2_lua_nargs(L) == depth, "the refused calls pushed nothing");
}

static void OnTick(void* L, void* ud) {
    (void)L;
    (void)ud;
    InterlockedIncrement(&g_ticks);
}

static DWORD WINAPI LuaThread(LPVOID p) {
    (void)p;
    m2_logf("installing the Lua host");
    if (!m2_lua_install()) {
        const char* e = m2_lua_last_error();
        m2_logf("FAIL: m2_lua_install: %s", e ? e : "(no message)");
        g_failures++;
        return 0;
    }
    Check(1, "m2_lua_install");
    Check(m2_lua_add_namespace("M2Smoke", kSmokeLib), "m2_lua_add_namespace(M2Smoke)");
    Check(!m2_lua_add_namespace("M2Smoke", kSmokeLib), "a second M2Smoke is refused");
    Check(m2_lua_on_state(OnState, NULL), "m2_lua_on_state");
    Check(m2_lua_on_tick(OnTick, NULL), "m2_lua_on_tick");
    return 0;
}

static void OnWorldLoaded(int reached, void* ud) {
    (void)ud;
    m2_logf("world loaded: phase %d (%s)", reached, m2_loadtrigger_phase_name(reached));
    Check(reached == M2_PHASE_REACHED_WORLD_IDX, "the trigger fired for the world-loaded phase");
    Check(m2_loadtrigger_current_phase() == reached, "m2_loadtrigger_current_phase agrees");
    Outcome("M2SmokeFP", M2_SHADER_OK);
    Outcome("M2SmokeFP_pl_sl", M2_SHADER_OK);
    Outcome("M2SmokeVP", M2_SHADER_OK);
    Outcome("M2SmokeDupFP_pl", M2_SHADER_ERR_DUPLICATE);
    Outcome("PgMeshVP", M2_SHADER_ERR_DUPLICATE);
    m2_logf("log lines seen by the listener: %d", (int)g_lines);
    Check(g_lines > 0, "the log-stream listener received lines");
    Check(m2_loghook_set_active(OnLine, NULL, 0), "m2_loghook_set_active pauses the listener");
    Check(!m2_loghook_set_active(OnLine, (void*)1, 0), "m2_loghook_set_active refuses an unknown listener");
    m2_logf("Lua ticks: %d", (int)g_ticks);
    Check(g_ticks > 0, "the Lua tick ran");
    Check(g_ran == 1, "chunks ran on a captured state");
    m2_logf("done: %d failure(s)", g_failures);
}

static void Queue(m2_shader_status st, const char* what) {
    char line[160];
    wsprintfA(line, "%s queued: %s", what, m2_shader_status_name(st));
    Check(st == M2_SHADER_OK, line);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    char path[MAX_PATH];
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    if (!m2_abi_ok()) return FALSE;

    m2_log_init(inst);
    m2_logf("m2_smoke: m2-sdk.dll %s (%d)", m2_version_string(), m2_version_num());

    m2_module_path(inst, "m2_smoke.log", path, sizeof(path));
    m2_logf("module path: %s", path);
    Check(lstrlenA(path) > 12, "m2_module_path builds a path beside the module");

    m2_logf("queueing shaders");
    Queue(m2_shader_add_pixel(M2_SHADER_FAMILY_BLUR_PIXEL, kNewPixel), "pixel M2SmokeFP");
    Queue(m2_shader_add_vertex(M2_SHADER_FAMILY_VERTEX, &kNewVertex), "vertex M2SmokeVP");
    Queue(m2_shader_add_pixel(M2_SHADER_FAMILY_BLUR_PIXEL, kDupPixel), "pixel PgBlurHFP (duplicate)");
    Queue(m2_shader_add_vertex(M2_SHADER_FAMILY_VERTEX, &kDupVertex), "vertex PgMeshVP (duplicate)");

    m2_logf("installing the log listener and the load trigger");
    Check(m2_loghook_add_listener(OnLine, NULL), "m2_loghook_add_listener");
    Check(m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, OnWorldLoaded, NULL),
          "m2_loadtrigger_on_phase");
    Check(m2_loadtrigger_install(), "m2_loadtrigger_install");
    Check(CreateThread(NULL, 0, LuaThread, NULL, 0, NULL) != NULL, "the Lua thread started");
    m2_logf("DllMain done; waiting for the world to load");
    return TRUE;
}
