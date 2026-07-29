/* AUTO-GENERATED from loadprobe by gen_ladder.py — DO NOT EDIT. */
/* Source of truth: loadprobe/src/phases.rs */
#ifndef M2_LOAD_LADDER_GEN_H
#define M2_LOAD_LADDER_GEN_H

#define M2_LADDER_COUNT 21
#define M2_PHASE_REACHED_WORLD_IDX 20
#define M2_PHASE_ENTERED_WORLD_IDX 11

typedef struct {
    int idx;
    const char* name;
    const char* const* matches;
    int match_count;
} M2LoadPhase;

/* The table itself lives in the DLL (load_ladder.gen.c), not here. Defined in a
 * header it would be duplicated into every consumer's translation unit — and a
 * `static` array cannot be exported, so the shared layer could not own it. Mods use
 * the M2_PHASE_* constants above; only m2_loadtrigger reads the table. */
extern const M2LoadPhase k_m2_ladder[M2_LADDER_COUNT];

#endif /* M2_LOAD_LADDER_GEN_H */
