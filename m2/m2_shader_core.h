/* m2_shader_core.h — the shader registry's platform-independent core. SDK-internal.
 *
 * Everything here is plain C over plain memory: the family table, the engine's name hash, the
 * pool key lookup, the queue and its validation, and the per-entry admission decision the
 * registry detour takes. m2_shader.c supplies the Windows side (signature checks, the MinHook
 * detour, the engine calls). test/shader_core builds this file on the host and tests it directly.
 */
#ifndef M2_SHADER_CORE_H
#define M2_SHADER_CORE_H

#include <stdint.h>
#include "m2_shader_types.h"

typedef enum m2_shader_stage {
    M2_SHADER_STAGE_PIXEL,
    M2_SHADER_STAGE_VERTEX
} m2_shader_stage;

typedef struct m2_shader_family_info {
    const char*     name;    /* family_table name, e.g. "blur_pixel" */
    m2_shader_stage stage;
    uint32_t        vtable;  /* VA of the family's 6-slot vtable */
    uint32_t        size;    /* record size in bytes */
} m2_shader_family_info;

/* The largest record size in the family table, per stage (hdr_flare_pixel, cloud_render_vertex).
 * m2_shader.c sizes its record storage by these. */
#define M2_SHADER_PS_RECORD_MAX 0x15Cu
#define M2_SHADER_VS_RECORD_MAX 0x16Cu

/* Pixel entries carry 4 classes (retail's plain, _pl, _sl, _pl_sl); vertex entries carry 1. */
#define M2_SHADER_PIXEL_CLASSES 4

typedef struct m2_shader_entry {
    m2_shader_family family;
    unsigned         classes;                        /* 4 for pixel, 1 for vertex */
    m2_shader_class  cls[M2_SHADER_PIXEL_CLASSES];
    uint32_t         key[M2_SHADER_PIXEL_CLASSES];   /* m2_shader_core_hash(cls[i].name) */
    void*            owner;                          /* the calling module, for its log line */
    int              processed;                      /* the registry pass recorded `outcome` */
    m2_shader_status outcome;                        /* valid when processed */
} m2_shader_entry;

/* The family's row, or NULL when `family` is not an m2_shader_family enumerator. */
const m2_shader_family_info* m2_shader_core_family(int family);

/* The enumerator's spelling ("M2_SHADER_ERR_DUPLICATE"), or NULL for a value outside the enum. */
const char* m2_shader_core_status_name(m2_shader_status s);

/* The engine's name hash, FUN_00824270: FNV-1a over the bytes, each byte sign-extended and OR'd
 * with 0x20, then a final round of `^ 0x2a`, `* 0x01000193`. Only called on non-empty names. */
uint32_t m2_shader_core_hash(const char* name);

/* FUN_008242b0: the slot holding `key` in a key table of `slots` entries, or -1. Linear probe from
 * key % slots, wrapping once around the table, stopping at an empty (0) slot. Key 0 always misses. */
int m2_shader_core_pool_find(uint32_t key, const uint32_t* keys, uint32_t slots);

/* Validate and queue one entry. Checks, in order: FAMILY (not an enumerator, or the wrong stage),
 * ARGUMENT (null/empty name or sho, sho not ending in ".sho" or over 128 characters, a name
 * hashing to 0), DUPLICATE
 * (a name repeated in the entry or already queued, by hash), CAPACITY (the queue already holds
 * 0x800 pixel or 0x100 vertex entries, more than the game's registry can take). */
m2_shader_status m2_shader_core_queue_pixel(m2_shader_family family,
                                            const m2_shader_class classes[M2_SHADER_PIXEL_CLASSES],
                                            void* owner);
m2_shader_status m2_shader_core_queue_vertex(m2_shader_family family, const m2_shader_class* cls,
                                             void* owner);

unsigned m2_shader_core_entry_count(void);
m2_shader_entry* m2_shader_core_entry(unsigned index);

/* How many of the entry's classes the game registers: every pixel class when `shader_level` is
 * non-zero, only class 0 when it is zero; a vertex entry's one class. */
unsigned m2_shader_core_registrations(const m2_shader_entry* e, uint8_t shader_level);

/* The detour's decision for one entry against the live pool of its stage. DUPLICATE when any of
 * the entry's names is already a key in `keys`; CAPACITY when `live_count` plus the entry's
 * registrations exceeds `slots`; otherwise OK. */
m2_shader_status m2_shader_core_admit(const m2_shader_entry* e, uint8_t shader_level,
                                      uint32_t live_count, const uint32_t* keys, uint32_t slots);

/* Record the registry pass's result for the entry. */
void m2_shader_core_record(m2_shader_entry* e, m2_shader_status outcome);

/* OK for a name whose entry registered, the recorded error for a name whose entry failed, and
 * ARGUMENT for a null or empty name, a name never queued, or a queued name the registry pass has
 * not reached. */
m2_shader_status m2_shader_core_outcome(const char* name);

#endif /* M2_SHADER_CORE_H */
