/* Generated from loadprobe::phases::LADDER by mercs2_mod_runtime's build, which writes it beside
 * libm2-sdk.dll.a. Do not edit: the m2-sdk Shipment's build fails when this copy differs. */
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

/* The table itself lives in the DLL and is not exported. Mods use the M2_PHASE_* constants above and
 * m2_loadtrigger_phase_name. */

#endif /* M2_LOAD_LADDER_GEN_H */
