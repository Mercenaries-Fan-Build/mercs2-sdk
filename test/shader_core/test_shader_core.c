/* Host tests for the shader registry's core (m2/m2_shader_core.c): the family table, status names,
 * the engine name hash, the pool key lookup, call-time validation, the registry pass's admission
 * decision, and m2_shader_outcome's answers.
 *
 * The core keeps one process-wide queue, as it does in the game, so the tests run in order and
 * every queued name is distinct across the whole file.
 */
#include <stdio.h>
#include <string.h>
#include "m2_shader_core.h"
#include "m2_target.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(cond)) {                                                                \
            g_failures++;                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                             \
    } while (0)

#define CHECK_STATUS(expr, want)                                                      \
    do {                                                                              \
        m2_shader_status got_ = (expr);                                               \
        g_checks++;                                                                   \
        if (got_ != (want)) {                                                         \
            g_failures++;                                                             \
            fprintf(stderr, "%s:%d: %s returned %s, expected %s\n", __FILE__, __LINE__, \
                    #expr, m2_shader_core_status_name(got_),                          \
                    m2_shader_core_status_name(want));                                \
        }                                                                             \
    } while (0)

static void test_family_table(void) {
    int f, pixel = 0, vertex = 0;
    for (f = 0; f < M2_SHADER_FAMILY_COUNT; f++) {
        const m2_shader_family_info* info = m2_shader_core_family(f);
        CHECK(info != NULL);
        CHECK(info->name != NULL && info->name[0] != '\0');
        CHECK(info->vtable >= M2_RDATA_START_VA && info->vtable < M2_RDATA_START_VA + M2_RDATA_SIZE);
        if (info->stage == M2_SHADER_STAGE_PIXEL) {
            pixel++;
            CHECK(f <= M2_SHADER_FAMILY_WATER_WAKE_PIXEL);
            CHECK(info->size <= M2_SHADER_PS_RECORD_MAX);
            /* FUN_0085ace0 writes through +0xf8. */
            CHECK(info->size >= 0xFC);
        } else {
            vertex++;
            CHECK(f >= M2_SHADER_FAMILY_VERTEX);
            CHECK(info->size <= M2_SHADER_VS_RECORD_MAX);
            /* FUN_0085ade0 writes through +0x110. */
            CHECK(info->size >= 0x114);
        }
    }
    CHECK(pixel == 26);
    CHECK(vertex == 19);
    CHECK(m2_shader_core_family(-1) == NULL);
    CHECK(m2_shader_core_family(M2_SHADER_FAMILY_COUNT) == NULL);

    /* The base families carry the vtables the base constructors store. */
    CHECK(m2_shader_core_family(M2_SHADER_FAMILY_PIXEL)->vtable == 0x00BE89ECu);
    CHECK(m2_shader_core_family(M2_SHADER_FAMILY_VERTEX)->vtable == 0x00BE8A04u);
    CHECK(strcmp(m2_shader_core_family(M2_SHADER_FAMILY_TONE_MAPPING_PIXEL)->name,
                 "tone_mapping_pixel") == 0);
    CHECK(m2_shader_core_family(M2_SHADER_FAMILY_TONE_MAPPING_PIXEL)->size == 0x13C);
    CHECK(strcmp(m2_shader_core_family(M2_SHADER_FAMILY_WATER_WAKE_VERTEX)->name,
                 "water_wake_vertex") == 0);
    CHECK(m2_shader_core_family(M2_SHADER_FAMILY_HDR_FLARE_PIXEL)->size == M2_SHADER_PS_RECORD_MAX);
    CHECK(m2_shader_core_family(M2_SHADER_FAMILY_CLOUD_RENDER_VERTEX)->size ==
          M2_SHADER_VS_RECORD_MAX);
}

static void test_status_names(void) {
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_OK), "M2_SHADER_OK") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_SIGNATURE), "M2_SHADER_ERR_SIGNATURE") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_HOOK), "M2_SHADER_ERR_HOOK") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_TOO_LATE), "M2_SHADER_ERR_TOO_LATE") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_ARGUMENT), "M2_SHADER_ERR_ARGUMENT") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_DUPLICATE), "M2_SHADER_ERR_DUPLICATE") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_FAMILY), "M2_SHADER_ERR_FAMILY") == 0);
    CHECK(strcmp(m2_shader_core_status_name(M2_SHADER_ERR_CAPACITY), "M2_SHADER_ERR_CAPACITY") == 0);
    CHECK(m2_shader_core_status_name((m2_shader_status)8) == NULL);
    CHECK(m2_shader_core_status_name((m2_shader_status)-1) == NULL);
}

static void test_hash(void) {
    /* The ASET type constants, verified against the EXE. */
    CHECK(m2_shader_core_hash("texture") == 0xF011157Au);
    CHECK(m2_shader_core_hash("model") == 0x5B724250u);
    CHECK(m2_shader_core_hash("TEXTURE") == m2_shader_core_hash("texture"));
    /* FUN_00824270 sign-extends each byte (movsx) before the OR, which matters from 0x80 up. */
    CHECK(m2_shader_core_hash("\xE9t\xE9") == 0x1449B483u);
}

static void test_pool_find(void) {
    uint32_t keys[8];
    memset(keys, 0, sizeof(keys));
    CHECK(m2_shader_core_pool_find(0x13, keys, 8) == -1);

    keys[3] = 0x13;                       /* 0x13 % 8 == 3: its home slot */
    CHECK(m2_shader_core_pool_find(0x13, keys, 8) == 3);
    CHECK(m2_shader_core_pool_find(0x0B, keys, 8) == -1);   /* home 3, then empty slot 4 */

    keys[7] = 0x1F;                       /* home 7 */
    keys[0] = 0x27;                       /* home 7, wrapped to 0 */
    CHECK(m2_shader_core_pool_find(0x27, keys, 8) == 0);
    CHECK(m2_shader_core_pool_find(0, keys, 8) == -1);

    {
        uint32_t full[4] = { 4, 5, 6, 7 };   /* every slot occupied */
        CHECK(m2_shader_core_pool_find(9, full, 4) == -1);
        CHECK(m2_shader_core_pool_find(7, full, 4) == 3);
    }
}

static const m2_shader_class kGlow[4] = {
    { "TestGlowFP", "TestGlowFP.sho" },
    { "TestGlowFP_pl", "TestGlowFP_pl.sho" },
    { "TestGlowFP_sl", "TestGlowFP_sl.sho" },
    { "TestGlowFP_pl_sl", "TestGlowFP_pl_sl.sho" },
};
static const m2_shader_class kGlowVs = { "TestGlowVP", "TestGlowVP.sho" };

static void test_family_checks(void) {
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_VERTEX, kGlow, NULL),
                 M2_SHADER_ERR_FAMILY);
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_WATER_WAKE_VERTEX, kGlow, NULL),
                 M2_SHADER_ERR_FAMILY);
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_PIXEL, &kGlowVs, NULL),
                 M2_SHADER_ERR_FAMILY);
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_WATER_WAKE_PIXEL, &kGlowVs, NULL),
                 M2_SHADER_ERR_FAMILY);
    CHECK_STATUS(m2_shader_core_queue_pixel((m2_shader_family)M2_SHADER_FAMILY_COUNT, kGlow, NULL),
                 M2_SHADER_ERR_FAMILY);
    CHECK_STATUS(m2_shader_core_queue_vertex((m2_shader_family)-1, &kGlowVs, NULL),
                 M2_SHADER_ERR_FAMILY);
    CHECK(m2_shader_core_entry_count() == 0);
}

static void test_argument_checks(void) {
    char long_sho[M2_SHADER_SHO_MAX_LEN + 2];
    m2_shader_class c = { "TestArgVP", "TestArgVP.sho" };
    m2_shader_class px[4];

    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, NULL, NULL),
                 M2_SHADER_ERR_ARGUMENT);
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_PIXEL, NULL, NULL),
                 M2_SHADER_ERR_ARGUMENT);

    c.name = NULL;
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);
    c.name = "";
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);
    c.name = "TestArgVP";
    c.sho = NULL;
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);
    c.sho = "";
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);
    c.sho = "TestArgVP.fx";
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);
    c.sho = "TestArgVP.SHO";
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);
    c.sho = ".sho";
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);

    /* 129 characters overflow the 128 that fit between record+0xb and the class at +0x8c. */
    memset(long_sho, 'a', sizeof(long_sho));
    memcpy(long_sho + M2_SHADER_SHO_MAX_LEN + 1 - 4, ".sho", 4);
    long_sho[M2_SHADER_SHO_MAX_LEN + 1] = '\0';
    c.sho = long_sho;
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL), M2_SHADER_ERR_ARGUMENT);

    /* One bad class rejects the whole pixel entry. */
    memcpy(px, kGlow, sizeof(px));
    px[3].sho = "TestGlowFP_pl_sl";
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_PIXEL, px, NULL), M2_SHADER_ERR_ARGUMENT);
    CHECK(m2_shader_core_entry_count() == 0);
}

static void test_queue_and_duplicates(void) {
    m2_shader_class px[4];
    static char long_ok[M2_SHADER_SHO_MAX_LEN + 1];
    m2_shader_class edge = { "TestEdgeVP", long_ok };

    /* Two classes of one pixel entry that differ only in case are one name to the engine. */
    memcpy(px, kGlow, sizeof(px));
    px[2].name = "testglowfp_PL";
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_PIXEL, px, NULL), M2_SHADER_ERR_DUPLICATE);
    memcpy(px, kGlow, sizeof(px));
    px[1].name = px[0].name;
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_PIXEL, px, NULL), M2_SHADER_ERR_DUPLICATE);
    CHECK(m2_shader_core_entry_count() == 0);

    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_BLUR_PIXEL, kGlow, NULL), M2_SHADER_OK);
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &kGlowVs, NULL), M2_SHADER_OK);
    CHECK(m2_shader_core_entry_count() == 2);

    /* A name already queued, in either stage and any case. */
    CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_PIXEL, kGlow, NULL), M2_SHADER_ERR_DUPLICATE);
    {
        m2_shader_class again = { "TESTGLOWFP_SL", "Other.sho" };
        CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_FX_VERTEX, &again, NULL),
                     M2_SHADER_ERR_DUPLICATE);
    }
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &kGlowVs, NULL),
                 M2_SHADER_ERR_DUPLICATE);
    CHECK(m2_shader_core_entry_count() == 2);

    /* Exactly 128 characters fit. */
    memset(long_ok, 'b', M2_SHADER_SHO_MAX_LEN);
    memcpy(long_ok + M2_SHADER_SHO_MAX_LEN - 4, ".sho", 4);
    long_ok[M2_SHADER_SHO_MAX_LEN] = '\0';
    CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_SKY_VERTEX, &edge, NULL), M2_SHADER_OK);
    CHECK(m2_shader_core_entry_count() == 3);

    {
        const m2_shader_entry* e = m2_shader_core_entry(0);
        CHECK(e->family == M2_SHADER_FAMILY_BLUR_PIXEL);
        CHECK(e->classes == 4);
        CHECK(e->key[1] == m2_shader_core_hash("TestGlowFP_pl"));
        CHECK(e->processed == 0);
        CHECK(m2_shader_core_entry(3) == NULL);
    }
}

static void test_outcome_before_pass(void) {
    CHECK_STATUS(m2_shader_core_outcome("NeverQueuedFP"), M2_SHADER_ERR_ARGUMENT);
    CHECK_STATUS(m2_shader_core_outcome(NULL), M2_SHADER_ERR_ARGUMENT);
    CHECK_STATUS(m2_shader_core_outcome(""), M2_SHADER_ERR_ARGUMENT);
    /* Queued, but the registry pass has not reached it. */
    CHECK_STATUS(m2_shader_core_outcome("TestGlowFP"), M2_SHADER_ERR_ARGUMENT);
}

static void test_admit(void) {
    static uint32_t ps_keys[M2_SHADER_PS_CAPACITY];
    static uint32_t vs_keys[M2_SHADER_VS_CAPACITY];
    const m2_shader_entry* px = m2_shader_core_entry(0);
    const m2_shader_entry* vx = m2_shader_core_entry(1);
    uint32_t k;

    memset(ps_keys, 0, sizeof(ps_keys));
    memset(vs_keys, 0, sizeof(vs_keys));

    CHECK(m2_shader_core_registrations(px, 1) == 4);
    CHECK(m2_shader_core_registrations(px, 0) == 1);
    CHECK(m2_shader_core_registrations(vx, 0) == 1);
    CHECK(m2_shader_core_registrations(vx, 1) == 1);

    CHECK_STATUS(m2_shader_core_admit(px, 1, 242, ps_keys, M2_SHADER_PS_CAPACITY), M2_SHADER_OK);
    CHECK_STATUS(m2_shader_core_admit(vx, 1, 129, vs_keys, M2_SHADER_VS_CAPACITY), M2_SHADER_OK);

    /* Capacity counts the live names plus the names this entry registers. */
    CHECK_STATUS(m2_shader_core_admit(px, 1, M2_SHADER_PS_CAPACITY - 4, ps_keys, M2_SHADER_PS_CAPACITY),
                 M2_SHADER_OK);
    CHECK_STATUS(m2_shader_core_admit(px, 1, M2_SHADER_PS_CAPACITY - 3, ps_keys, M2_SHADER_PS_CAPACITY),
                 M2_SHADER_ERR_CAPACITY);
    CHECK_STATUS(m2_shader_core_admit(px, 0, M2_SHADER_PS_CAPACITY - 1, ps_keys, M2_SHADER_PS_CAPACITY),
                 M2_SHADER_OK);
    CHECK_STATUS(m2_shader_core_admit(px, 0, M2_SHADER_PS_CAPACITY, ps_keys, M2_SHADER_PS_CAPACITY),
                 M2_SHADER_ERR_CAPACITY);
    CHECK_STATUS(m2_shader_core_admit(vx, 0, M2_SHADER_VS_CAPACITY, vs_keys, M2_SHADER_VS_CAPACITY),
                 M2_SHADER_ERR_CAPACITY);

    /* Any of the entry's names already live in the registry, whatever the ShaderLevel. */
    k = m2_shader_core_hash("TestGlowFP_pl_sl");
    ps_keys[k % M2_SHADER_PS_CAPACITY] = k;
    CHECK_STATUS(m2_shader_core_admit(px, 1, 243, ps_keys, M2_SHADER_PS_CAPACITY),
                 M2_SHADER_ERR_DUPLICATE);
    CHECK_STATUS(m2_shader_core_admit(px, 0, 243, ps_keys, M2_SHADER_PS_CAPACITY),
                 M2_SHADER_ERR_DUPLICATE);
    /* The stages keep separate registries. */
    CHECK_STATUS(m2_shader_core_admit(vx, 1, 129, ps_keys, M2_SHADER_VS_CAPACITY), M2_SHADER_OK);
}

static void test_outcome_after_pass(void) {
    m2_shader_core_record(m2_shader_core_entry(0), M2_SHADER_OK);
    m2_shader_core_record(m2_shader_core_entry(1), M2_SHADER_ERR_DUPLICATE);

    CHECK_STATUS(m2_shader_core_outcome("TestGlowFP"), M2_SHADER_OK);
    CHECK_STATUS(m2_shader_core_outcome("testglowfp_pl_sl"), M2_SHADER_OK);
    CHECK_STATUS(m2_shader_core_outcome("TestGlowVP"), M2_SHADER_ERR_DUPLICATE);
    CHECK_STATUS(m2_shader_core_outcome("TestEdgeVP"), M2_SHADER_ERR_ARGUMENT);
    CHECK_STATUS(m2_shader_core_outcome("NeverQueuedFP"), M2_SHADER_ERR_ARGUMENT);
}

static void test_queue_capacity(void) {
    static char names[M2_SHADER_VS_CAPACITY + 1][16];
    static char shos[M2_SHADER_VS_CAPACITY + 1][20];
    unsigned i, before = m2_shader_core_entry_count();
    m2_shader_status last = M2_SHADER_OK;

    /* Two vertex entries are queued already; fill the vertex queue to 0x100 entries. */
    for (i = 0; i + 2 < M2_SHADER_VS_CAPACITY; i++) {
        m2_shader_class c;
        snprintf(names[i], sizeof(names[i]), "TestCapVP%u", i);
        snprintf(shos[i], sizeof(shos[i]), "TestCapVP%u.sho", i);
        c.name = names[i];
        c.sho = shos[i];
        last = m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL);
        if (last != M2_SHADER_OK) break;
    }
    CHECK_STATUS(last, M2_SHADER_OK);
    CHECK(m2_shader_core_entry_count() == before + M2_SHADER_VS_CAPACITY - 2);
    {
        m2_shader_class c = { "TestCapVPOver", "TestCapVPOver.sho" };
        CHECK_STATUS(m2_shader_core_queue_vertex(M2_SHADER_FAMILY_VERTEX, &c, NULL),
                     M2_SHADER_ERR_CAPACITY);
    }
    /* The pixel queue is separate. */
    {
        static const m2_shader_class more[4] = {
            { "TestMoreFP", "TestMoreFP.sho" },
            { "TestMoreFP_pl", "TestMoreFP_pl.sho" },
            { "TestMoreFP_sl", "TestMoreFP_sl.sho" },
            { "TestMoreFP_pl_sl", "TestMoreFP_pl_sl.sho" },
        };
        CHECK_STATUS(m2_shader_core_queue_pixel(M2_SHADER_FAMILY_FX_PIXEL, more, NULL), M2_SHADER_OK);
    }
}

int main(void) {
    test_family_table();
    test_status_names();
    test_hash();
    test_pool_find();
    test_family_checks();
    test_argument_checks();
    test_queue_and_duplicates();
    test_outcome_before_pass();
    test_admit();
    test_outcome_after_pass();
    test_queue_capacity();

    if (g_failures) {
        fprintf(stderr, "FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("OK: %d checks passed\n", g_checks);
    return 0;
}
