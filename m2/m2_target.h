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

/* Spawn / guidmap registry detour (see m2_spawn_registry.c).
 *
 * FUN_004CC130 — the component-type-singleton get-or-create (__thiscall: this=ECX,
 * param_2 at [esp+4]). Its occupied-slot path returns the existing singleton and
 * DROPS param_2, so a PATCH overlay's block-3185 additions (new spawn templates /
 * name->handle / guidmap handles) never register. The detour force-calls the create
 * for its additive side-effects while leaving the base instance in place.
 *
 * FUN_008242b0 — the hash-probe helper the original uses to map a type key to a slot
 * index. NON-STANDARD convention (verified from the call site at 0x004CC130, not the
 * heuristic Ghidra signature): ECX = *param_2 (the type key), ESI = this+0x500C (the
 * key-table base), one caller-cleaned stack arg (0x1400 = table capacity), idx in EAX. */
#define M2_TYPE_SINGLETON_GETCREATE_VA  0x004CC130u
#define M2_TYPE_HASH_INDEX_VA           0x008242B0u
#define M2_TYPE_HASH_TABLE_OFFSET       0x500Cu       /* this+0x500C -> ESI for the probe */
#define M2_TYPEID_GUIDMAP               0x140E8728u
#define M2_TYPEID_WORLDENTITY           0x5647C35Du

/* Registry bucket-insert into the global name / guidmap registries — the REAL insert path,
 * probed (observe-only) to see whether a handle ever reaches it and into which table.
 * FUN_00649180 is __stdcall (verified: terminates `ret 0x14`, 5 dword args callee-cleaned),
 * signature (int* table, u32 key, u32 aux, int key2, void* val). The two global registry
 * roots are the VAs the caller passes as `table`. */
#define M2_REGISTRY_INSERT_VA           0x00649180u
#define M2_REGISTRY_NAME_TABLE_VA       0x00DF6B88u   /* &PTR_PTR_00DF6B88 */
#define M2_REGISTRY_GUIDMAP_TABLE_VA    0x00DF6C08u   /* &PTR_PTR_00DF6C08 */

/* The name registry is a STATICALLY-constructed global object (its init assigns
 * PTR_PTR_00df6b88 = &PTR_FUN_00bc4800), so *(0x00DF6B88) == 0x00BC4800 is a cheap "map
 * intact" gate before injecting. Element stride is `u16 [map+0x24]`; the map's copy slot
 * (0x54) memcpy's exactly that many bytes of the record (it does NOT copy the name string —
 * the record's field0 char* is stored by reference, so the string must outlive the entry). */
#define M2_NAME_REG_VTABLE_VA           0x00BC4800u
#define M2_NAME_REG_STRIDE_OFF          0x24u
#define M2_NAME_REG_COUNT_OFF           0x18u

/* Component-signature registry (0x00DF6D08): the per-entity 256-bit component bitmask store that
 * FUN_00665590 fills (one bit per attached component) and the post-spawn "notify all systems" loop
 * FUN_004F7210 reads. It is the SAME keyed-container class as the name registry (FUN_00649180 insert:
 * FUN_00665590 calls FUN_00649180(&PTR_PTR_00df6d08, handle, 0, 0, sig)), so it shares the +0x18 count
 * / +0x24 stride field offsets and its element stride is 0x20 (eight dwords = 256 bits). A novel spawn
 * handle with NO record here makes the finalizer's lookup miss (ebx=0) → C0000005 @0x004F72BA. An
 * ALL-ZERO signature is safe: the loop's `je` skips every system. Its vtable lives in .rdata (like the
 * name registry's 0xBC4800), so a "*table in [rdata,rdata+size)" check gates injection without a
 * hard-coded value. */
#define M2_SIG_REG_TABLE_VA             0x00DF6D08u

/* ── M2: the resident/worldentity COMP loader (post-load replay target) ────────────────────
 * FUN_00654940 — the ECS COMP-block loader. __cdecl(ctx, flag) (verified: `ret`, caller-cleans;
 * arg1 = ctx at [esp+0x130] in the prologue; the `0` second arg is unused). It drives the whole
 * additive load of one container: mints each key's dense index on demand (FUN_006654b0), inserts
 * the reflection records + reads the flgs signature into 0xDF6D08 (FUN_00649180), and instantiates
 * the subgraph (FUN_00673070) — never reconstructs a pool, so feeding it a MINIMAL block (one new
 * handle's records) adds exactly that entity. Its context is a ~68-byte struct (verified from
 * FUN_00464780 disasm): +0x08 chunk COUNT, +0x0c DATA-BASE ptr, +0x10 CHUNK-ARRAY ptr (desc rows,
 * stride 0x14 {fourcc,row_off,size,w3,w4}), +0x18 inline READER {vtable@0, len@+8, cursor@+0x10,
 * base@+0x18, flags@+0x20}. FUN_00464780 seeks via reader.vtable[0x28] then FUN_00825e40 (which fully
 * (re)sets base/len/cursor); FUN_00654940 reads bulk via reader.vtable[0x14](dst,n,0). */
#define M2_COMP_LOADER_VA               0x00654940u
#define M2_CHUNK_READER_VA              0x00464780u   /* GetChunkDataReader(ctx, EAX=idx) */
#define M2_READER_POSITION_VA           0x00825E40u   /* positions reader: base=EDI, len=EAX, cursor=0 */
#define M2_CTX_COUNT_OFF                0x08u
#define M2_CTX_DATABASE_OFF             0x0Cu
#define M2_CTX_CHUNKARR_OFF             0x10u
#define M2_CTX_READER_OFF               0x18u

/* ── Stage B: live ECS component-pool injection (give the spawn template real COMPs) ──────
 * Each reflection component owns a paged-hash instance pool keyed by the 32-bit entity handle.
 * Pool object base = (registrar `name@` ptr) - 0x24 — verified against SceneObject's 0x017c02f0
 * (docs/mercs2-ecs/06_world_terrain_roads_streaming.md gives every class's name@). Field offsets
 * within a pool object (from FUN_0064a600 / FUN_0064a090): +0x08 capacity, +0x0c stride(s16),
 * +0x0e page_shift(u8), +0x10 recs-per-page (bucket mask = this-1), +0x14 mult 0x9e3779b9,
 * +0x1c key table (parallel; -1 = empty), +0x20 page table. The component's signature bit-id is a
 * s16 at pool-0x14 (0xffff/-1 = unset → FUN_00665590 treats it as a safe no-op).
 * Insert = FUN_0064a600(EDI=pool; stack: key, record; callee-cleaned). Probe = FUN_0064a090
 * (ECX=pool, ESI=key -> bucket in EAX). Signature bit-set = FUN_00665590(key, bit_id) __cdecl. */
#define M2_SCENEOBJECT_POOL_VA  0x017C02F0u   /* SceneObject 0xB6185886, stride 0x1C (28 B) */
#define M2_MODEL_POOL_VA        0x017BFF80u   /* Model 0x5B724250, stride 0x04 (payload = mesh hash) */
#define M2_LC_MODEL_HASH        0x592057C4u   /* the landing-craft mesh block */
#define M2_BOX_HANDLE           0x80000002u   /* donor: "box" static prop (carries SceneObject + Model) */
#define M2_POOL_PROBE_VA        0x0064A090u
#define M2_POOL_INSERT_VA       0x0064A600u
#define M2_SIG_SETBIT_VA        0x00665590u

/* The two global component-descriptor pointer arrays (FUN_0064aa10 walks both): authored classes
 * then Runtime/bit-3 classes. Each element is a descriptor base; descriptor+0x10 = type_hash,
 * descriptor+0x48 = the pool object (verified: Model desc 0x017bff38 + 0x48 = pool 0x017bff80). The
 * enumeration probe walks these to discover exactly which pools a donor handle actually lives in. */
#define M2_DESC_ARRAY_A_VA   0x00EDBEC8u   /* &PTR_PTR_00edbec8 (authored) */
#define M2_DESC_ARRAY_A_CNT  0x01176058u   /* DAT_01176058 */
#define M2_DESC_ARRAY_B_VA   0x00EDBAC8u   /* &PTR_PTR_00edbac8 (Runtime*) */
#define M2_DESC_ARRAY_B_CNT  0x0117605Cu   /* DAT_0117605c */
#define M2_DESC_TYPEHASH_OFF 0x10u
#define M2_DESC_POOL_OFF     0x48u

#endif /* M2_TARGET_H */
