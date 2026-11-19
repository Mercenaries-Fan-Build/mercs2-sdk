/* A minimal consumer, built exactly the way a real mod is.
 *
 * This exists to prove the thing that is easy to get wrong and impossible to notice until someone's
 * game refuses to start: that a mod including <m2.h> and linking $(M2_LDFLAGS) produces a loadable
 * 32-bit .asi whose import table names m2-sdk.dll.
 *
 * It never runs here — CI cross-compiles it and inspects the PE. Compiling is not the interesting
 * part; the IMPORT TABLE is. A wrong M2_API direction, a missing export, or a name-mangling change
 * all still compile and only fail at load time on someone else's machine.
 */
#include "m2.h"

/* Shaders queued for the game's shader registry. The strings are literals because the registry
 * reads them after DllMain returns. */
static const m2_shader_class kConsumerPixel[4] = {
    { "ConsumerFP", "ConsumerFP.sho" },
    { "ConsumerFP_pl", "ConsumerFP_pl.sho" },
    { "ConsumerFP_sl", "ConsumerFP_sl.sho" },
    { "ConsumerFP_pl_sl", "ConsumerFP_pl_sl.sho" },
};
static const m2_shader_class kConsumerVertex = { "ConsumerVP", "ConsumerVP.sho" };

/* A callback a script hands over (Consumer.Later(fn)), kept by ref and called on a later tick. */
static int g_later = 0;

static int Later(void* L) {
    if (g_later > 0) m2_lua_unref(L, g_later);
    g_later = m2_lua_ref(L, 1);
    if (g_later == 0) m2_logf("m2_lua_ref: %s", m2_lua_last_error());
    return 0;
}

static const m2_lua_reg kConsumerLib[] = {
    { "Later", Later },
    { NULL, NULL },
};

static void OnTick(void* L, void* ud) {
    int ref = g_later;
    (void)ud;
    if (ref <= 0 || !m2_lua_state_ok(L)) return;
    g_later = 0;
    if (m2_lua_push_ref(L, ref)) {
        m2_lua_push_string(L, "later", -1);
        if (m2_lua_pcall(L, 1, 0) != 0) m2_logf("m2_lua_pcall: %s", m2_lua_last_error());
    } else {
        m2_logf("m2_lua_push_ref: %s", m2_lua_last_error());
    }
    if (!m2_lua_unref(L, ref)) m2_logf("m2_lua_unref: %s", m2_lua_last_error());
}

static void OnWorldLoaded(int reached_idx, void* ud) {
    (void)ud;
    m2_logf("world reached phase %d (%s)", reached_idx, m2_loadtrigger_phase_name(reached_idx));
    m2_logf("ConsumerFP: %s", m2_shader_status_name(m2_shader_outcome("ConsumerFP")));
    m2_logf("ConsumerVP: %s", m2_shader_status_name(m2_shader_outcome("ConsumerVP")));
    /* Off DllMain: the calls wait for the game's code to be decrypted. */
    if (m2_lua_install()) {
        m2_lua_add_namespace("Consumer", kConsumerLib);
        m2_lua_on_tick(OnTick, NULL);
    } else {
        m2_logf("m2_lua_install: %s", m2_lua_last_error());
    }
    {
        m2_tree_sites t;
        t.size = sizeof t;
        if (m2_render_tree_sites(&t)) m2_logf("tree Z-prepass loop %p", t.zpass_loop);
        else m2_logf("m2_render_tree_sites: %s", m2_render_last_error());
    }
    {
        m2_display_mode_sites d;
        unsigned char code[32], patch[9];
        int desktop = 0;
        d.size = sizeof d;
        if (m2_render_display_mode_sites(&d)) {
            m2_logf("display-mode refresh test %p, %u bytes of filter code", d.refresh_cmp,
                    m2_render_display_mode_filter(code, 255, 255, code, sizeof code, patch));
            m2_logf("picked mode %d", m2_render_pick_display_mode(NULL, 0, 1920, 1080, 0, 0, &desktop));
        } else {
            m2_logf("m2_render_display_mode_sites: %s", m2_render_last_error());
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    /* Refuse to load against an m2-sdk.dll older than the header we compiled against, rather than
     * calling an export whose signature may have changed. The loader binds by name only. */
    if (!m2_abi_ok()) return FALSE;

    m2_log_init(inst);
    m2_logf("consumer smoke test, m2 %s", m2_version_string());

    /* Before the renderer constructor runs FUN_0084f130, which builds the shader registry. */
    {
        m2_shader_status st = m2_shader_add_pixel(M2_SHADER_FAMILY_BLUR_PIXEL, kConsumerPixel);
        if (st != M2_SHADER_OK) m2_logf("m2_shader_add_pixel: %s", m2_shader_status_name(st));
        st = m2_shader_add_vertex(M2_SHADER_FAMILY_VERTEX, &kConsumerVertex);
        if (st != M2_SHADER_OK) m2_logf("m2_shader_add_vertex: %s", m2_shader_status_name(st));
    }

    m2_hook_init();
    m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, OnWorldLoaded, NULL);
    m2_loadtrigger_install();
    return TRUE;
}
