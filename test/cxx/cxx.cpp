/* A C++ mod: every header's functions must bind to m2-sdk.dll's C exports. Without the headers'
 * extern "C" blocks a C++ mod compiles and then fails to link, against mangled names (_Z10m2_lua_refPvi)
 * the DLL does not export. One call from each header that declares a function; never run. */
#include "m2.h"

static int Callback(void* L) {
    int ref = m2_lua_ref(L, 1);                                  /* m2_lua.h */
    if (ref > 0 && m2_lua_push_ref(L, ref)) m2_lua_pcall(L, 0, 0);
    m2_lua_unref(L, ref);
    return m2_lua_nargs(L);                                      /* m2_luastack.h */
}

static void OnLine(const char* msg, void* ud) {
    (void)msg;
    (void)ud;
}

static void OnPaused(void* L, void* ud) {
    (void)L;
    (void)ud;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    if (!m2_abi_ok()) return FALSE;                              /* m2_version.h */
    m2_log_init(inst);                                           /* m2_log.h */
    m2_logf("C++ consumer, m2 %s", m2_version_string());         /* m2_log.h, m2_api.h's __ImageBase */
    m2_hook_init();                                              /* m2_hook.h */
    m2_logf("ini %d", m2_ini_bool("1"));                         /* m2_ini.h */
    m2_logf("phase %d", m2_loadtrigger_current_phase());         /* m2_loadtrigger.h */
    m2_loghook_add_listener(OnLine, NULL);                       /* m2_loghook.h */
    m2_lua_debug_on_paused(OnPaused, NULL);                      /* m2_lua_debug.h */
    m2_logf("%s", m2_shader_status_name(M2_SHADER_OK));          /* m2_shader.h */
    m2_logf("%s", m2_render_last_error() ? "render error" : ""); /* m2_render.h */
    static const m2_lua_reg kLib[] = { { "Callback", Callback }, { NULL, NULL } };
    m2_lua_add_namespace("Cxx", kLib);
    return TRUE;
}
