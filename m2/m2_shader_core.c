/* The shader registry's platform-independent core. See m2_shader_core.h. */
#include "m2_shader_core.h"
#include "m2_target.h"
#include <string.h>

/* One row per record family, indexed by m2_shader_family. Each family is one vtable in the retail
 * EXE whose slot [1] is FUN_0085ac90 and slot [2] the stage's load handler (0x0085b1a0 pixel,
 * 0x0085af00 vertex). The size is the extent both the static initializer and the constant binder
 * write; the array strides of the static records (0xfc pixel, 0x104 decal_pixel) agree. */
static const m2_shader_family_info g_families[M2_SHADER_FAMILY_COUNT] = {
    [M2_SHADER_FAMILY_PIXEL] = { "pixel", M2_SHADER_STAGE_PIXEL, 0x00BE89ECu, 0xFCu },
    [M2_SHADER_FAMILY_ADAPTIVE_LUMINANCE_PIXEL] = { "adaptive_luminance_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAADD0u, 0x10Cu },
    [M2_SHADER_FAMILY_ANTI_ALIASING_PIXEL] = { "anti_aliasing_pixel", M2_SHADER_STAGE_PIXEL, 0x00BE85B4u, 0x11Cu },
    [M2_SHADER_FAMILY_BILLBOARD_TREE_PIXEL] = { "billboard_tree_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAC888u, 0x104u },
    [M2_SHADER_FAMILY_BLOB_SHADOW_PIXEL] = { "blob_shadow_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAB148u, 0x114u },
    [M2_SHADER_FAMILY_BLUR_PIXEL] = { "blur_pixel", M2_SHADER_STAGE_PIXEL, 0x00BE8A58u, 0x104u },
    [M2_SHADER_FAMILY_CLOUD_GEN_PIXEL] = { "cloud_gen_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAB59Cu, 0x11Cu },
    [M2_SHADER_FAMILY_CLOUD_RENDER_PIXEL] = { "cloud_render_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAB584u, 0x14Cu },
    [M2_SHADER_FAMILY_COMPOSITE_PIXEL] = { "composite_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAAEDCu, 0x104u },
    [M2_SHADER_FAMILY_DECAL_PIXEL] = { "decal_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAC614u, 0x104u },
    [M2_SHADER_FAMILY_DOWN_SAMPLE_PIXEL] = { "down_sample_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAADE8u, 0x104u },
    [M2_SHADER_FAMILY_FX_PIXEL] = { "fx_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAC314u, 0x104u },
    [M2_SHADER_FAMILY_HDR_FLARE_PIXEL] = { "hdr_flare_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAADA0u, 0x15Cu },
    [M2_SHADER_FAMILY_MOTION_BLUR_PIXEL] = { "motion_blur_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAAE84u, 0x11Cu },
    [M2_SHADER_FAMILY_RAIN_PIXEL] = { "rain_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAAF8Cu, 0x10Cu },
    [M2_SHADER_FAMILY_RIBBON_PIXEL] = { "ribbon_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAC410u, 0xFCu },
    [M2_SHADER_FAMILY_SCALEFORM_CXFORM_PIXEL] = { "scaleform_cxform_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAD158u, 0x10Cu },
    [M2_SHADER_FAMILY_SCALEFORM_SOLID_COLOR_PIXEL] = { "scaleform_solid_color_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAD170u, 0x104u },
    [M2_SHADER_FAMILY_SCALEFORM_STRIP_PIXEL] = { "scaleform_strip_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAD128u, 0x10Cu },
    [M2_SHADER_FAMILY_SCALEFORM_TEXT_TEXTURE_PIXEL] = { "scaleform_text_texture_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAD140u, 0x104u },
    [M2_SHADER_FAMILY_SHIMMER_PIXEL] = { "shimmer_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAAE5Cu, 0x10Cu },
    [M2_SHADER_FAMILY_SKY_PIXEL] = { "sky_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAB604u, 0x104u },
    [M2_SHADER_FAMILY_TONE_MAPPING_PIXEL] = { "tone_mapping_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAADB8u, 0x13Cu },
    [M2_SHADER_FAMILY_WATER_HEIGHT_MAP_PIXEL] = { "water_height_map_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAB97Cu, 0x104u },
    [M2_SHADER_FAMILY_WATER_PIXEL] = { "water_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAC168u, 0x10Cu },
    [M2_SHADER_FAMILY_WATER_WAKE_PIXEL] = { "water_wake_pixel", M2_SHADER_STAGE_PIXEL, 0x00BAB964u, 0x104u },
    [M2_SHADER_FAMILY_VERTEX] = { "vertex", M2_SHADER_STAGE_VERTEX, 0x00BE8A04u, 0x114u },
    [M2_SHADER_FAMILY_BILLBOARD_TREE_INSTANCE_VERTEX] = { "billboard_tree_instance_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC870u, 0x11Cu },
    [M2_SHADER_FAMILY_BILLBOARD_TREE_VERTEX] = { "billboard_tree_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC858u, 0x11Cu },
    [M2_SHADER_FAMILY_BLOB_SHADOW_VERTEX] = { "blob_shadow_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAB130u, 0x12Cu },
    [M2_SHADER_FAMILY_CLOUD_RENDER_VERTEX] = { "cloud_render_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAB56Cu, 0x16Cu },
    [M2_SHADER_FAMILY_DECAL_VERTEX] = { "decal_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC5FCu, 0x124u },
    [M2_SHADER_FAMILY_FX_VERTEX] = { "fx_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC2FCu, 0x114u },
    [M2_SHADER_FAMILY_MESH_COMBINER_VERTEX] = { "mesh_combiner_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC6B0u, 0x114u },
    [M2_SHADER_FAMILY_RAIN_VERTEX] = { "rain_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAAF74u, 0x12Cu },
    [M2_SHADER_FAMILY_RIBBON_VERTEX] = { "ribbon_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC3F8u, 0x114u },
    [M2_SHADER_FAMILY_ROAD_VERTEX] = { "road_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC448u, 0x12Cu },
    [M2_SHADER_FAMILY_SCALEFORM_GLYPH_VERTEX] = { "scaleform_glyph_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAD110u, 0x11Cu },
    [M2_SHADER_FAMILY_SCALEFORM_STRIP_VERTEX] = { "scaleform_strip_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAD0F8u, 0x124u },
    [M2_SHADER_FAMILY_SCRUB_VERTEX] = { "scrub_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC97Cu, 0x12Cu },
    [M2_SHADER_FAMILY_SKY_VERTEX] = { "sky_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAB5ECu, 0x11Cu },
    [M2_SHADER_FAMILY_SUN_VERTEX] = { "sun_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAB61Cu, 0x124u },
    [M2_SHADER_FAMILY_TERRAIN_MESH_VERTEX] = { "terrain_mesh_vertex", M2_SHADER_STAGE_VERTEX, 0x00BACCD8u, 0x124u },
    [M2_SHADER_FAMILY_WATER_VERTEX] = { "water_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAC150u, 0x134u },
    [M2_SHADER_FAMILY_WATER_WAKE_VERTEX] = { "water_wake_vertex", M2_SHADER_STAGE_VERTEX, 0x00BAB94Cu, 0x11Cu },
};

static const char* const g_statusNames[] = {
    "M2_SHADER_OK",
    "M2_SHADER_ERR_SIGNATURE",
    "M2_SHADER_ERR_HOOK",
    "M2_SHADER_ERR_TOO_LATE",
    "M2_SHADER_ERR_ARGUMENT",
    "M2_SHADER_ERR_DUPLICATE",
    "M2_SHADER_ERR_FAMILY",
    "M2_SHADER_ERR_CAPACITY",
};

/* Every entry adds at least one name to its stage's registry, so a queue holding more entries of
 * a stage than that stage's capacity holds an entry that can never register. */
#define M2_SHADER_QUEUE_MAX (M2_SHADER_PS_CAPACITY + M2_SHADER_VS_CAPACITY)

static m2_shader_entry g_queue[M2_SHADER_QUEUE_MAX];
static unsigned g_queueCount = 0;
static unsigned g_queuedPixel = 0;
static unsigned g_queuedVertex = 0;

const m2_shader_family_info* m2_shader_core_family(int family) {
    if (family < 0 || family >= M2_SHADER_FAMILY_COUNT) return NULL;
    return &g_families[family];
}

const char* m2_shader_core_status_name(m2_shader_status s) {
    if ((int)s < 0 || (unsigned)s >= sizeof(g_statusNames) / sizeof(g_statusNames[0])) return NULL;
    return g_statusNames[s];
}

uint32_t m2_shader_core_hash(const char* name) {
    uint32_t h = 0x811C9DC5u;
    const unsigned char* p = (const unsigned char*)name;
    for (; *p; p++) {
        /* movsx ecx, cl; or ecx, 0x20 */
        h ^= (uint32_t)((int32_t)(signed char)*p) | 0x20u;
        h *= 0x01000193u;
    }
    h ^= 0x2Au;
    h *= 0x01000193u;
    return h;
}

int m2_shader_core_pool_find(uint32_t key, const uint32_t* keys, uint32_t slots) {
    uint32_t start, i;
    if (key == 0 || slots == 0) return -1;
    start = key % slots;
    for (i = 0; i < slots; i++) {
        uint32_t slot = (start + i) % slots;
        if (keys[slot] == key) return (int)slot;
        if (keys[slot] == 0) return -1;
    }
    return -1;
}

static int EndsWithSho(const char* s) {
    size_t n = strlen(s);
    return n > 4 && memcmp(s + n - 4, ".sho", 4) == 0;
}

static m2_shader_status CheckClass(const m2_shader_class* c) {
    if (!c->name || !c->name[0]) return M2_SHADER_ERR_ARGUMENT;
    if (!c->sho || !c->sho[0]) return M2_SHADER_ERR_ARGUMENT;
    if (!EndsWithSho(c->sho)) return M2_SHADER_ERR_ARGUMENT;
    if (strlen(c->sho) > M2_SHADER_SHO_MAX_LEN) return M2_SHADER_ERR_ARGUMENT;
    /* The pool marks an empty slot with key 0, so a name hashing to 0 cannot be stored. */
    if (m2_shader_core_hash(c->name) == 0) return M2_SHADER_ERR_ARGUMENT;
    return M2_SHADER_OK;
}

static const m2_shader_entry* FindQueued(uint32_t key) {
    unsigned i, c;
    for (i = 0; i < g_queueCount; i++) {
        for (c = 0; c < g_queue[i].classes; c++) {
            if (g_queue[i].key[c] == key) return &g_queue[i];
        }
    }
    return NULL;
}

static m2_shader_status Queue(m2_shader_family family, m2_shader_stage stage,
                              const m2_shader_class* classes, unsigned count, void* owner) {
    const m2_shader_family_info* info = m2_shader_core_family((int)family);
    m2_shader_entry e;
    unsigned i, j;

    if (!info || info->stage != stage) return M2_SHADER_ERR_FAMILY;
    if (!classes) return M2_SHADER_ERR_ARGUMENT;
    for (i = 0; i < count; i++) {
        m2_shader_status st = CheckClass(&classes[i]);
        if (st != M2_SHADER_OK) return st;
    }

    memset(&e, 0, sizeof(e));
    e.family = family;
    e.classes = count;
    e.owner = owner;
    for (i = 0; i < count; i++) {
        e.cls[i] = classes[i];
        e.key[i] = m2_shader_core_hash(classes[i].name);
        for (j = 0; j < i; j++) {
            if (e.key[j] == e.key[i]) return M2_SHADER_ERR_DUPLICATE;
        }
        if (FindQueued(e.key[i])) return M2_SHADER_ERR_DUPLICATE;
    }

    if (stage == M2_SHADER_STAGE_PIXEL && g_queuedPixel >= M2_SHADER_PS_CAPACITY)
        return M2_SHADER_ERR_CAPACITY;
    if (stage == M2_SHADER_STAGE_VERTEX && g_queuedVertex >= M2_SHADER_VS_CAPACITY)
        return M2_SHADER_ERR_CAPACITY;

    g_queue[g_queueCount++] = e;
    if (stage == M2_SHADER_STAGE_PIXEL) g_queuedPixel++;
    else g_queuedVertex++;
    return M2_SHADER_OK;
}

m2_shader_status m2_shader_core_queue_pixel(m2_shader_family family,
                                            const m2_shader_class classes[M2_SHADER_PIXEL_CLASSES],
                                            void* owner) {
    return Queue(family, M2_SHADER_STAGE_PIXEL, classes, M2_SHADER_PIXEL_CLASSES, owner);
}

m2_shader_status m2_shader_core_queue_vertex(m2_shader_family family, const m2_shader_class* cls,
                                             void* owner) {
    return Queue(family, M2_SHADER_STAGE_VERTEX, cls, 1, owner);
}

unsigned m2_shader_core_entry_count(void) {
    return g_queueCount;
}

m2_shader_entry* m2_shader_core_entry(unsigned index) {
    if (index >= g_queueCount) return NULL;
    return &g_queue[index];
}

unsigned m2_shader_core_registrations(const m2_shader_entry* e, uint8_t shader_level) {
    if (e->classes == M2_SHADER_PIXEL_CLASSES && shader_level == 0) return 1;
    return e->classes;
}

m2_shader_status m2_shader_core_admit(const m2_shader_entry* e, uint8_t shader_level,
                                      uint32_t live_count, const uint32_t* keys, uint32_t slots) {
    unsigned c;
    for (c = 0; c < e->classes; c++) {
        if (m2_shader_core_pool_find(e->key[c], keys, slots) >= 0) return M2_SHADER_ERR_DUPLICATE;
    }
    if (live_count > slots || slots - live_count < m2_shader_core_registrations(e, shader_level))
        return M2_SHADER_ERR_CAPACITY;
    return M2_SHADER_OK;
}

void m2_shader_core_record(m2_shader_entry* e, m2_shader_status outcome) {
    e->outcome = outcome;
    e->processed = 1;
}

m2_shader_status m2_shader_core_outcome(const char* name) {
    const m2_shader_entry* e;
    if (!name || !name[0]) return M2_SHADER_ERR_ARGUMENT;
    e = FindQueued(m2_shader_core_hash(name));
    if (!e || !e->processed) return M2_SHADER_ERR_ARGUMENT;
    return e->outcome;
}
