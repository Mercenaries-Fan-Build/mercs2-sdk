/* m2_target.h — binary-specific addresses for the target Mercenaries 2 EXE.
 *
 * All hardcoded VAs live here so there is ONE place to retarget if the executable
 * changes. Verified against the cracked retail EXE (53,482,288 bytes, image base
 * 0x00400000) — see the mercenaries-game RE notes.
 */
#ifndef M2_TARGET_H
#define M2_TARGET_H

/* Section layout (image base 0x00400000). */
#define M2_RDATA_START_VA   0x00B05000u
#define M2_RDATA_SIZE       0x000F1000u
#define M2_TEXT_START_VA    0x00401000u
#define M2_TEXT_SIZE        0x00703000u

/* Shared no-op log stub (33 C0 C3 = xor eax,eax; ret). ~700 stripped log fns —
 * including Lua print / Debug.Printf — funnel through here. MinHooking this one
 * .text site captures the entire log stream and is SecuROM-safe (a .rdata reg-slot
 * patch trips anti-tamper; see tools/pmc_blackbox/lua_log_hook.c). */
#define M2_LOG_STUB_VA      0x006D5640u

/* VO Lua bindings (native code). CERTAIN per docs/lua_engine_bindings_audit.md. */
#define M2_VO_CUE_VA                0x005E9DE0u
#define M2_VO_CUEWITHOUTSUBS_VA     0x005E9F40u

/* Shader registration (m2_shader.h). Read from the unpacked EXE and emulated under all 8 caps /
 * ShaderLevel configurations; see the shader-registration RE notes.
 *
 * FUN_0084f130: builds the shader registry. __thiscall (ECX = renderer), one stack argument,
 * `ret 4`. Its only caller is FUN_007492d0 @0x0074957A, the renderer constructor. */
#define M2_SHADER_REGISTER_ALL_VA   0x0084F130u
/* FUN_0085ac90: __thiscall(record, const char* name, const char* sho, int class), `ret 0xc`.
 * Sets record+4 = hash(name), record+0x8c = class, strcpy(record+0xb, sho), then calls the load
 * handler vtbl[+8] synchronously, which assigns the record's index in the pool. */
#define M2_SHADER_REGISTER_VA       0x0085AC90u
/* FUN_0085ace0: pixel-record base constructor. __thiscall (ECX = record), no stack args, `ret`. */
#define M2_SHADER_PS_BASE_CTOR_VA   0x0085ACE0u
/* FUN_0085ade0: vertex-record base constructor. usercall with EAX = record, no stack args, `ret`.
 * Writes through EAX and clobbers ECX only. */
#define M2_SHADER_VS_BASE_CTOR_VA   0x0085ADE0u

/* The first 16 bytes of each function above. The shader registry checks them before hooking. */
#define M2_SHADER_REGISTER_ALL_PROLOGUE \
    { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x53, 0x56, 0x68, 0x38, 0x7A, 0x97, 0x01, 0xB8, 0x3C, 0x34 }
#define M2_SHADER_REGISTER_PROLOGUE \
    { 0x8B, 0x54, 0x24, 0x04, 0x56, 0x57, 0x8B, 0xF1, 0xE8, 0xD3, 0x95, 0xFC, 0xFF, 0x89, 0x46, 0x04 }
#define M2_SHADER_PS_BASE_CTOR_PROLOGUE \
    { 0x8B, 0xC1, 0x33, 0xC9, 0x66, 0xC7, 0x40, 0x08, 0xFF, 0xFF, 0xC6, 0x40, 0x0A, 0x01, 0x89, 0x88 }
#define M2_SHADER_VS_BASE_CTOR_PROLOGUE \
    { 0x33, 0xC9, 0x66, 0xC7, 0x40, 0x08, 0xFF, 0xFF, 0xC6, 0x40, 0x0A, 0x01, 0x89, 0x88, 0x90, 0x00 }
#define M2_SHADER_PROLOGUE_LEN      16u

/* The shader pool (DAT_01977a38), zeroed by its static initializer FUN_0085ab00. The counts are
 * incremented only by the inserts FUN_0085b7c0 (pixel) and FUN_00632250 (vertex). */
#define M2_SHADER_POOL_VA           0x01977A38u
#define M2_SHADER_PS_COUNT_VA       0x01977A38u   /* u32, pool+0 */
#define M2_SHADER_PS_KEYS_VA        0x01979A40u   /* u32[0x800] hash keys, pool+0x2008 */
#define M2_SHADER_VS_COUNT_VA       0x0197DA40u   /* u32, pool+0x6008 */
#define M2_SHADER_VS_KEYS_VA        0x0197DE48u   /* u32[0x100] hash keys, pool+0x6410 */
/* Slot counts of the two key tables. The inserts probe `& 0x7ff` / `& 0xff` without giving up, so
 * an insert into a full table never terminates. */
#define M2_SHADER_PS_CAPACITY       0x800u
#define M2_SHADER_VS_CAPACITY       0x100u

/* u8 in the render settings block 0x00dfc320: ShaderLevel after the caps check (FUN_00753280,
 * FUN_0074c7ac). Retail registers pixel classes 1-3 only when it is non-zero. */
#define M2_SHADER_LEVEL_VA          0x00DFC345u

/* Record layout written by FUN_0085ac90: the sho name is copied to +0xb and the class lives at
 * +0x8c, so a sho name holds at most 0x8c - 0xb - 1 = 128 characters before its terminator. */
#define M2_SHADER_SHO_MAX_LEN       128u

#endif /* M2_TARGET_H */
