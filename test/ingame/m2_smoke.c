/* An in-game smoke test of m2-sdk.dll: install m2_smoke.asi in the game's scripts/ folder beside
 * m2-sdk.dll, load into the world, and read m2_smoke.log next to it. Each check writes one line, and
 * every step is logged before it runs, so a crash shows where it stopped.
 *
 * Checks: the version and per-module log; m2_module_path; the shader registry (two new entries that
 * load retail .sho files, which must register, and two that reuse a retail shader name, which the
 * registry must refuse as DUPLICATE); a log-stream listener; a load trigger on the world being fully
 * loaded, which reads back every shader outcome.
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
static int g_failures = 0;

static void Check(int ok, const char* what) {
    if (!ok) g_failures++;
    m2_logf("%s: %s", ok ? "PASS" : "FAIL", what);
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
    m2_logf("DllMain done; waiting for the world to load");
    return TRUE;
}
