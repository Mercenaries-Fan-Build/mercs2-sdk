/* m2_shader_types.h — the value types of the shader registry (m2_shader.h).
 *
 * Plain C with no Windows dependency, so the registry's validation core (m2_shader_core.c) builds
 * and tests on any host. Mods include m2_shader.h, which includes this.
 */
#ifndef M2_SHADER_TYPES_H
#define M2_SHADER_TYPES_H

typedef enum m2_shader_status {
    M2_SHADER_OK = 0,
    /* A function the registry calls or hooks does not start with the expected bytes: this is not
     * the EXE build m2_target.h describes. */
    M2_SHADER_ERR_SIGNATURE,
    /* MinHook could not install the detour on FUN_0084f130. */
    M2_SHADER_ERR_HOOK,
    /* The game has started building its shader registry, so a queued entry can never register. */
    M2_SHADER_ERR_TOO_LATE,
    /* A null or empty name or sho, a sho that does not end in ".sho" or is longer than 128
     * characters, or a name whose hash is 0 (the pool's empty-slot marker); from
     * m2_shader_outcome, a name with no registration outcome. */
    M2_SHADER_ERR_ARGUMENT,
    /* The name is already queued, already in the game's registry, or repeated within one pixel
     * entry. Names compare by the engine's case-folded hash. */
    M2_SHADER_ERR_DUPLICATE,
    /* Not an m2_shader_family, or a family of the other stage (a vertex family passed to
     * m2_shader_add_pixel, or a pixel family to m2_shader_add_vertex). */
    M2_SHADER_ERR_FAMILY,
    /* The entry's names do not fit the game's registry: 0x800 pixel names, 0x100 vertex names. */
    M2_SHADER_ERR_CAPACITY
} m2_shader_status;

/* One row per shader record family (one vtable each) in the retail EXE. The values are fixed: qm
 * writes headers that use these enumerators. */
typedef enum m2_shader_family {
    M2_SHADER_FAMILY_PIXEL = 0,
    M2_SHADER_FAMILY_ADAPTIVE_LUMINANCE_PIXEL = 1,
    M2_SHADER_FAMILY_ANTI_ALIASING_PIXEL = 2,
    M2_SHADER_FAMILY_BILLBOARD_TREE_PIXEL = 3,
    M2_SHADER_FAMILY_BLOB_SHADOW_PIXEL = 4,
    M2_SHADER_FAMILY_BLUR_PIXEL = 5,
    M2_SHADER_FAMILY_CLOUD_GEN_PIXEL = 6,
    M2_SHADER_FAMILY_CLOUD_RENDER_PIXEL = 7,
    M2_SHADER_FAMILY_COMPOSITE_PIXEL = 8,
    M2_SHADER_FAMILY_DECAL_PIXEL = 9,
    M2_SHADER_FAMILY_DOWN_SAMPLE_PIXEL = 10,
    M2_SHADER_FAMILY_FX_PIXEL = 11,
    M2_SHADER_FAMILY_HDR_FLARE_PIXEL = 12,
    M2_SHADER_FAMILY_MOTION_BLUR_PIXEL = 13,
    M2_SHADER_FAMILY_RAIN_PIXEL = 14,
    M2_SHADER_FAMILY_RIBBON_PIXEL = 15,
    M2_SHADER_FAMILY_SCALEFORM_CXFORM_PIXEL = 16,
    M2_SHADER_FAMILY_SCALEFORM_SOLID_COLOR_PIXEL = 17,
    M2_SHADER_FAMILY_SCALEFORM_STRIP_PIXEL = 18,
    M2_SHADER_FAMILY_SCALEFORM_TEXT_TEXTURE_PIXEL = 19,
    M2_SHADER_FAMILY_SHIMMER_PIXEL = 20,
    M2_SHADER_FAMILY_SKY_PIXEL = 21,
    M2_SHADER_FAMILY_TONE_MAPPING_PIXEL = 22,
    M2_SHADER_FAMILY_WATER_HEIGHT_MAP_PIXEL = 23,
    M2_SHADER_FAMILY_WATER_PIXEL = 24,
    M2_SHADER_FAMILY_WATER_WAKE_PIXEL = 25,
    M2_SHADER_FAMILY_VERTEX = 26,
    M2_SHADER_FAMILY_BILLBOARD_TREE_INSTANCE_VERTEX = 27,
    M2_SHADER_FAMILY_BILLBOARD_TREE_VERTEX = 28,
    M2_SHADER_FAMILY_BLOB_SHADOW_VERTEX = 29,
    M2_SHADER_FAMILY_CLOUD_RENDER_VERTEX = 30,
    M2_SHADER_FAMILY_DECAL_VERTEX = 31,
    M2_SHADER_FAMILY_FX_VERTEX = 32,
    M2_SHADER_FAMILY_MESH_COMBINER_VERTEX = 33,
    M2_SHADER_FAMILY_RAIN_VERTEX = 34,
    M2_SHADER_FAMILY_RIBBON_VERTEX = 35,
    M2_SHADER_FAMILY_ROAD_VERTEX = 36,
    M2_SHADER_FAMILY_SCALEFORM_GLYPH_VERTEX = 37,
    M2_SHADER_FAMILY_SCALEFORM_STRIP_VERTEX = 38,
    M2_SHADER_FAMILY_SCRUB_VERTEX = 39,
    M2_SHADER_FAMILY_SKY_VERTEX = 40,
    M2_SHADER_FAMILY_SUN_VERTEX = 41,
    M2_SHADER_FAMILY_TERRAIN_MESH_VERTEX = 42,
    M2_SHADER_FAMILY_WATER_VERTEX = 43,
    M2_SHADER_FAMILY_WATER_WAKE_VERTEX = 44
} m2_shader_family;

#define M2_SHADER_FAMILY_COUNT 45

/* One shader: the name the engine keys it by, and the .sho file the engine loads for it.
 *
 * The registry keeps both pointers and reads them when the game builds its registry, after
 * DllMain returns, so both must stay valid for the life of the process (string literals do). */
typedef struct m2_shader_class {
    const char* name;
    const char* sho;
} m2_shader_class;

#endif /* M2_SHADER_TYPES_H */
