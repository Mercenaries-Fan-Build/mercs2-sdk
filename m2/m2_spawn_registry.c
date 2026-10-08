/* Detour on the component-type-singleton get-or-create so a PATCH overlay's additions register.
 *
 * The hook site FUN_004CC130 (__thiscall) decompiles to, in effect:
 *
 *     type_in  = *(param_2 + 4);
 *     type_mgr = vtbl[1](this);                       // slot 1 = *(*this + 4)
 *     if (type_in != type_mgr) return 0;
 *     idx  = FUN_008242b0(0x1400);                    // hash-probe, see below
 *     slot = (idx < 0) ? (int*)this + 2 : (int*)this + idx + 3;
 *     if (*slot == 0) return vtbl[7](this, param_2);  // first create; slot 7 = *(*this + 0x1c)
 *     return *slot;                                   // ← occupied: returns existing, DROPS param_2
 *
 * That last line is the bug this module fixes for the guidmap / worldentity singletons.
 *
 * ── The FUN_008242b0 calling convention (why there is inline asm here) ──────────────────
 * Ghidra labels FUN_008242b0 __thiscall(param_1,param_2), but the machine code at the call
 * site inside 0x004CC130 tells the real story:
 *
 *     mov  ecx, [ebp]          ; ecx = *param_2                (the type key)
 *     lea  ebx, [edi+4]        ; ebx = this+4
 *     push 0x1400              ;      = table capacity (one caller-cleaned stack arg)
 *     lea  esi, [ebx+0x5008]   ; esi = this+0x500C             (the key-table base)
 *     call FUN_008242b0        ; -> eax = slot index
 *     add  esp, 4              ; caller cleans -> cdecl stack discipline
 *
 * The helper reads its inputs from ECX and ESI, which a plain C call cannot set, so idx is
 * reproduced with a small asm wrapper that lays the registers out exactly as the game does.
 * Getting idx right matters: a wrong idx points `slot` at the wrong dword, so `*slot` (the
 * value we hand back as the existing singleton) would be garbage.
 */
#include "m2_spawn_registry.h"
#include "m2_target.h"
#include "m2_log.h"
#include "m2_ini.h"
#include "m2_hook.h"
#include "m2_loadtrigger.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>

/* First 16 bytes at 0x004CC130 for the running build. Verified before attaching; on any
 * mismatch we refuse to hook rather than detour whatever else lives at that address. */
static const unsigned char k_prologue[16] = {
    0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56, 0x8B,
    0x75, 0x04, 0x57, 0x8B, 0xF9, 0x8B, 0x07, 0x8B
};

/* First 16 bytes at 0x00649180 (the registry bucket-insert). */
static const unsigned char k_insert_prologue[16] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x53, 0x8B,
    0x5D, 0x08, 0x56, 0x57, 0x83, 0xCF, 0xFF, 0xF6
};

/* The two keys we care about: our landing_craft handle, and pandemic_hash_m2("landing_craft"). */
#define M2_LANDING_CRAFT_HANDLE  0x8000B3C5u
#define M2_LANDING_CRAFT_HASH    0x46AE2245u

/* INI-driven gates.
 *   enabled          — master switch (default on)
 *   probe_insert      — install the observe-only inserter probe (default on: this is the point)
 *   force_gate        — install the FUN_004CC130 force-create detour (default OFF: proven wrong
 *                       site — it never took a gated hit — kept behind a flag, not fired)
 *   hook_guidmap/_worldentity — per-type gates for the force-create path (only matter if
 *                       force_gate=1). */
static int g_enabled          = 1;
static int g_probe_insert     = 1;
static int g_force_gate       = 0;
static int g_inject_name      = 1;   /* the real fix: inject landing_craft's name->handle record */
static int g_inject_signature = 1;   /* Stage A: inject a zeroed 256-bit component signature so
                                      *          Pg.Spawn's finalizer doesn't null-deref (see below) */
static int g_inject_template  = 1;   /* Stage B: give the template real COMP records (SceneObject+Model) */
static int g_template_write   = 0;   /* Stage B write-gate: 0 = observe/log only, 1 = actually inject */
static unsigned g_enum1 = 0, g_enum2 = 0, g_enum3 = 0;  /* extra handles to enumerate (INI hex) */
static int g_hook_guidmap     = 1;
static int g_hook_worldentity = 1;

/* The name string is stored in the registry BY POINTER (the copy slot memcpy's the record but
 * never the string), so it must outlive the entry. A static const in this always-loaded DLL does. */
static const char g_landing_craft_name[] = "landing_craft";

/* Registry bucket-insert (FUN_00649180) — __stdcall(int* table, key, aux, key2, val) -> int. */
typedef int (__stdcall *registry_insert_fn)(int* table, unsigned key, unsigned aux,
                                            int key2, void* val);
static registry_insert_fn g_insert_orig = NULL;

/* Call-original trampoline: FUN_004CC130 is __thiscall(this, param_2) -> int. */
typedef int (__thiscall *singleton_getcreate_fn)(void* thisp, void* param_2);
static singleton_getcreate_fn g_orig = NULL;

/* vtbl[1] = get-type-id (__thiscall, no args); vtbl[7] = create (__thiscall, arg param_2). */
typedef int  (__thiscall *vtbl_gettype_fn)(void* thisp);
typedef void (__thiscall *vtbl_create_fn)(void* thisp, void* param_2);

/* Reproduce `idx = FUN_008242b0(0x1400)` with the game's exact register setup
 * (ECX = *param_2, ESI = this+0x500C, one caller-cleaned stack arg). ECX/ESI are tied to
 * output operands so GCC treats them as clobbered by the callee. */
static int spawn_hash_index(void* this_ecx, void* param_2) {
    unsigned key   = *(unsigned*)param_2;
    void*    table = (char*)this_ecx + M2_TYPE_HASH_TABLE_OFFSET;
    void*    fn    = (void*)M2_TYPE_HASH_INDEX_VA;
    int      idx;
    unsigned scratch_c;
    void*    scratch_s;
    __asm__ __volatile__(
        "pushl $0x1400\n\t"
        "call  *%[fn]\n\t"
        "addl  $4, %%esp\n\t"
        : "=a"(idx), "=c"(scratch_c), "=S"(scratch_s)
        : "1"(key), "2"(table), [fn]"r"(fn)
        : "edx", "cc", "memory");
    return idx;
}

/* The detour. Declared __fastcall so ECX(this)/EDX land as the first two params and the
 * lone stack arg (param_2) as the third — matching the __thiscall the game makes and the
 * `ret 4` cleanup the original performs. */
static int __fastcall spawn_registry_detour(void* this_ecx, void* edx, void* param_2) {
    unsigned type_in;
    unsigned type_mgr;
    void**   vtbl;
    int      is_guid, is_world, gated;
    int      idx;
    int*     slot;
    int      existing;

    (void)edx;

    vtbl     = *(void***)this_ecx;
    type_in  = *(unsigned*)((char*)param_2 + 4);
    type_mgr = (unsigned)((vtbl_gettype_fn)vtbl[1])(this_ecx);

    /* Not this manager's type, or not a type we own: behave exactly like the original. */
    if (type_in != type_mgr) return g_orig(this_ecx, param_2);

    is_guid  = (type_mgr == M2_TYPEID_GUIDMAP);
    is_world = (type_mgr == M2_TYPEID_WORLDENTITY);
    gated    = (is_guid && g_hook_guidmap) || (is_world && g_hook_worldentity);
    if (!gated) return g_orig(this_ecx, param_2);

    idx  = spawn_hash_index(this_ecx, param_2);
    slot = (idx < 0) ? ((int*)this_ecx + 2) : ((int*)this_ecx + idx + 3);

    /* DIAGNOSTIC: every gated hit with its slot state, so one relaunch reveals hit count,
     * empty-vs-occupied, and load-time-vs-gameplay timing. */
    m2_logf("[spawn_registry] gated hit: type=0x%08X (%s) idx=%d *slot=0x%08X",
            type_mgr, is_guid ? "guidmap" : "worldentity", idx, (unsigned)*slot);

    /* Empty slot = the base's FIRST create. Let the original do it unchanged. */
    if (*slot == 0) return g_orig(this_ecx, param_2);

    /* Occupied: base's singleton already lives here. Force the create for its additive
     * side-effects (new keys append into the global registries 0xDF6B88 / 0xDF6C08; base
     * keys update in place), then leave the base instance in the slot and return it — so
     * every existing caller sees exactly the pointer it saw before. */
    existing = *slot;
    ((vtbl_create_fn)vtbl[7])(this_ecx, param_2);
    m2_logf("[spawn_registry] forced create: type=0x%08X (%s) slot=0x%08X existing=0x%08X",
            type_mgr, is_guid ? "guidmap" : "worldentity",
            (unsigned)(UINT_PTR)slot, (unsigned)existing);
    return existing;
}

/* The template-handle table our handle 0x8000B3C5 was confirmed registered in (shape matches
 * box 0x80000002, key2=3). Characterized already — EXCLUDED from capture now; we need names. */
#define M2_REGISTRY_SEEN_TABLE_VA  0x00DF8610u
#define M2_REGISTRY_NEAR_RANGE     0x40u        /* treat table within +0x40 of a root as "that registry" */
#define M2_PATTERN_MAX             12           /* cap the name-capture log volume */

static volatile LONG g_pattern_seen = 0;

/* Read 16 bytes at p only if the page is committed and readable — a `val` payload may be a
 * transient stack pointer, so never fault the game just to inspect it. */
static int safe_read16(const void* p, unsigned char* out) {
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned char* base;
    SIZE_T avail;
    if (!p) return 0;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    base  = (const unsigned char*)mbi.BaseAddress;
    avail = (SIZE_T)((base + mbi.RegionSize) - (const unsigned char*)p);
    if (avail < 16) return 0;
    memcpy(out, p, 16);
    return 1;
}

/* Format 16 raw bytes as "AA BB CC ...". out must hold >= 48 bytes. */
static void hex16(const unsigned char* b, char* out) {
    static const char H[] = "0123456789ABCDEF";
    int i, j = 0;
    for (i = 0; i < 16; i++) {
        out[j++] = H[b[i] >> 4];
        out[j++] = H[b[i] & 0xF];
        out[j++] = ' ';
    }
    out[j - 1] = '\0';
}

/* Observe-only detour on the registry bucket-insert. Calls the original UNCHANGED. Two logs:
 *   (1) our-key hits (landing_craft handle 0x8000B3C5 / hash 0x46AE2245), plus a dump of the
 *       16-byte value payload so we understand what `val` points at;
 *   (2) name-capture: the first MAX inserts whose `table` is at/near the NAME-registry root
 *       0xDF6B88 (+0x40) ONLY — the handle table 0xDF8610 and guidmap 0xDF6C08 are already
 *       characterized and deliberately EXCLUDED. Each dumps 16 bytes at `val` too, so we see
 *       whether `val` is the handle, a record pointer, or a name string — the exact
 *       (key,aux,key2,val) convention to replicate for name-hash 0x46AE2245 -> handle
 *       0x8000B3C5. An empty capture across a full load is itself the finding: the name
 *       registry is populated by some function other than FUN_00649180. */
static int __stdcall registry_insert_probe(int* table, unsigned key, unsigned aux,
                                           int key2, void* val) {
    unsigned t = (unsigned)(UINT_PTR)table;
    int in_name  = (t >= M2_REGISTRY_NAME_TABLE_VA    && t <= M2_REGISTRY_NAME_TABLE_VA    + M2_REGISTRY_NEAR_RANGE);
    int in_guid  = (t >= M2_REGISTRY_GUIDMAP_TABLE_VA && t <= M2_REGISTRY_GUIDMAP_TABLE_VA + M2_REGISTRY_NEAR_RANGE);
    int in_seen  = (t == M2_REGISTRY_SEEN_TABLE_VA);

    if (key  == M2_LANDING_CRAFT_HANDLE || key  == M2_LANDING_CRAFT_HASH ||
        (unsigned)key2 == M2_LANDING_CRAFT_HANDLE || (unsigned)key2 == M2_LANDING_CRAFT_HASH) {
        unsigned char buf[16];
        char hex[48];
        const char* which = in_name ? "name(DF6B88)" : in_guid ? "guidmap(DF6C08)"
                          : in_seen ? "DF8610" : "other";
        /* If our name-hash lands as a NAME insert, landing_craft's name IS registered and the
         * nil GetGuidByName is a different problem — flag it loudly. */
        int is_name_hash_into_name =
            (key == M2_LANDING_CRAFT_HASH || (unsigned)key2 == M2_LANDING_CRAFT_HASH) && in_name;
        m2_logf("[spawn_registry] INSERT (our key)%s: table=0x%08X (%s) key=0x%08X aux=0x%08X "
                "key2=0x%08X val=0x%08X",
                is_name_hash_into_name ? " *** NAME-HASH INTO NAME REGISTRY ***" : "",
                t, which, key, aux, (unsigned)key2, (unsigned)(UINT_PTR)val);
        if (safe_read16(val, buf)) {
            hex16(buf, hex);
            m2_logf("[spawn_registry]   val@0x%08X -> %s", (unsigned)(UINT_PTR)val, hex);
        } else {
            m2_logf("[spawn_registry]   val@0x%08X -> <unreadable>", (unsigned)(UINT_PTR)val);
        }
    }

    /* NAME registry ONLY this pass (exclude 0xDF8610 handle table and 0xDF6C08 guidmap). */
    (void)in_guid;
    (void)in_seen;
    if (in_name) {
        LONG n = InterlockedIncrement(&g_pattern_seen);
        if (n <= M2_PATTERN_MAX) {
            unsigned char buf[16];
            char hex[48];
            m2_logf("[spawn_registry] name #%d: table=0x%08X (dName=0x%08X) "
                    "key=0x%08X aux=0x%08X key2=0x%08X val=0x%08X",
                    (int)n, t, (unsigned)(t - M2_REGISTRY_NAME_TABLE_VA),
                    key, aux, (unsigned)key2, (unsigned)(UINT_PTR)val);
            if (safe_read16(val, buf)) {
                hex16(buf, hex);
                m2_logf("[spawn_registry]   name#%d val@0x%08X -> %s",
                        (int)n, (unsigned)(UINT_PTR)val, hex);
            } else {
                m2_logf("[spawn_registry]   name#%d val@0x%08X -> <unreadable>",
                        (int)n, (unsigned)(UINT_PTR)val);
            }
        }
    }

    return g_insert_orig(table, key, aux, key2, val);
}

/* ── THE REAL FIX: inject the missing name->handle record for landing_craft ─────────────
 *
 * The handle 0x8000B3C5 and its template are already live in the handle table 0xDF8610; the
 * only missing piece is the NAME registry record that GetGuidByName(name) reads. We replicate
 * exactly what base's own name inserters do (FUN_005ccaf0 / FUN_006569b0):
 *
 *     FUN_00649180(&nameRegistry, HANDLE, NAMEHASH, NAMEHASH, &record)
 *
 * where `record` is the map element whose first field is a char* to the name string, followed
 * by the handle and name-hash (mirroring base's consecutive stack locals). FUN_00649180 is
 * insert-if-absent, so re-registering an existing key is a no-op — safe to call unconditionally.
 *
 * Fired once from the world-fully-loaded phase, after base has finished populating the registry.
 *
 * Containment: MinGW has no __try/__except, so safety rests on strong pre-validation instead —
 * the map is a statically-constructed global (so *map == its known vtable), the inserter's
 * prologue is signature-checked, and the element stride is bounds-checked before the memcpy. */
static void m2_spawn_registry_inject_name(int reached_idx, void* ud) {
    int*           map = (int*)M2_REGISTRY_NAME_TABLE_VA;
    unsigned       vtable, stride;
    int            count_before, count_after, ret;
    unsigned char  record[64];   /* zeroed; first 16 bytes carry {name, handle, hash, hash} */

    (void)reached_idx;
    (void)ud;

    if (!g_inject_name) {
        m2_logf("[spawn_registry] inject-name disabled via ini — skipped");
        return;
    }

    /* Gate 1: the name-registry global must be the object we reverse-engineered. */
    vtable = *(unsigned*)map;
    if (vtable != M2_NAME_REG_VTABLE_VA) {
        m2_logf("[spawn_registry] inject-name: name registry vtable 0x%08X != 0x%08X — aborted",
                vtable, (unsigned)M2_NAME_REG_VTABLE_VA);
        return;
    }

    /* Gate 2: the inserter must still be the function we characterized. */
    if (memcmp((void*)M2_REGISTRY_INSERT_VA, k_insert_prologue, sizeof(k_insert_prologue)) != 0) {
        m2_logf("[spawn_registry] inject-name: inserter prologue mismatch — aborted");
        return;
    }

    /* Gate 3: the element stride must be sane (record buffer holds up to 64 bytes). */
    stride = *(unsigned short*)((char*)map + M2_NAME_REG_STRIDE_OFF);
    if (stride < 4 || stride > sizeof(record)) {
        m2_logf("[spawn_registry] inject-name: unexpected element stride %u — aborted", stride);
        return;
    }

    /* Build the record: field0 = char* name (stored by reference), then handle + name-hash,
     * mirroring base's stack element. Zero the tail so the memcpy of `stride` bytes is defined. */
    memset(record, 0, sizeof(record));
    *(const char**)(record + 0) = g_landing_craft_name;
    *(unsigned*)(record + 4)    = M2_LANDING_CRAFT_HANDLE;
    *(unsigned*)(record + 8)    = M2_LANDING_CRAFT_HASH;
    *(unsigned*)(record + 12)   = M2_LANDING_CRAFT_HASH;

    count_before = *(int*)((char*)map + M2_NAME_REG_COUNT_OFF);
    m2_logf("[spawn_registry] inject-name: pre-insert count=%d stride=%u name='%s' "
            "handle=0x%08X hash=0x%08X",
            count_before, stride, g_landing_craft_name,
            M2_LANDING_CRAFT_HANDLE, M2_LANDING_CRAFT_HASH);

    /* insert-if-absent; same __stdcall(int*,key,aux,key2,val) shape base uses for name inserts. */
    ret = ((registry_insert_fn)M2_REGISTRY_INSERT_VA)(
              map, M2_LANDING_CRAFT_HANDLE, M2_LANDING_CRAFT_HASH,
              (int)M2_LANDING_CRAFT_HASH, record);

    count_after = *(int*)((char*)map + M2_NAME_REG_COUNT_OFF);
    m2_logf("[spawn_registry] inject-name: post-insert count=%d ret=%d -> %s",
            count_after, ret,
            count_after > count_before ? "INSERTED (was absent)" : "already present (no-op)");
}

/* ── STAGE A: inject a zeroed component-signature record for the spawn handle ────────────
 *
 * Pg.Spawn(name) resolves name->handle (now live via inject_name), then the SecuROM spawn worker
 * builds the entity and the finalizer FUN_004F8DA0 calls FUN_004F7210 ("entity added -> notify all
 * systems"). That loop reads the entity's 256-bit component-signature record out of registry
 * 0x00DF6D08; a handle with NO record there yields ebx=0 and null-derefs at 0x004F72BA (the exact
 * crash we captured). The engine normally creates this record lazily on the first component attach
 * (FUN_00665590). Since our template body isn't in the live pools yet, no component attaches and no
 * record is created — so we create it ourselves, replicating FUN_00665590's own insert verbatim:
 *
 *     FUN_00649180(&sigRegistry, HANDLE, 0, 0, &zeroed_256bit)
 *
 * An ALL-ZERO signature is safe by construction: FUN_004F7210 ANDs each system's required-component
 * bit against the signature and takes `je` (skip) when the AND is zero, so ZERO systems are notified
 * and none of their per-component handlers run. This proves the spawn path completes; the spawned
 * entity is inert/invisible (no components) until Stage B injects the real COMP records. Same three
 * gates as inject_name, plus the sig-registry vtable must point into .rdata. */
static void m2_spawn_registry_inject_signature(int reached_idx, void* ud) {
    int*           map = (int*)M2_SIG_REG_TABLE_VA;
    unsigned       vtable, stride;
    int            count_before, count_after, ret;
    unsigned char  record[64];   /* zeroed; first 0x20 bytes = the 256-bit signature (no bits set) */

    (void)reached_idx;
    (void)ud;

    if (!g_inject_signature) {
        m2_logf("[spawn_registry] inject-signature disabled via ini — skipped");
        return;
    }

    /* Gate 1: the signature registry's vtable must be a real .rdata pointer (we don't hard-code the
     * exact value — unlike the name registry — so log it for the record and sanity-range it). */
    vtable = *(unsigned*)map;
    if (vtable < M2_RDATA_START_VA || vtable >= M2_RDATA_START_VA + M2_RDATA_SIZE) {
        m2_logf("[spawn_registry] inject-signature: sig registry vtable 0x%08X not in .rdata — aborted",
                vtable);
        return;
    }

    /* Gate 2: the inserter must still be the function we characterized (shared with inject_name). */
    if (memcmp((void*)M2_REGISTRY_INSERT_VA, k_insert_prologue, sizeof(k_insert_prologue)) != 0) {
        m2_logf("[spawn_registry] inject-signature: inserter prologue mismatch — aborted");
        return;
    }

    /* Gate 3: the element stride must be sane (a 256-bit signature is 0x20 bytes; allow slack). */
    stride = *(unsigned short*)((char*)map + M2_NAME_REG_STRIDE_OFF);
    if (stride < 4 || stride > sizeof(record)) {
        m2_logf("[spawn_registry] inject-signature: unexpected element stride %u — aborted", stride);
        return;
    }

    memset(record, 0, sizeof(record));   /* all component bits clear = notify no systems */

    count_before = *(int*)((char*)map + M2_NAME_REG_COUNT_OFF);
    m2_logf("[spawn_registry] inject-signature: pre-insert count=%d stride=%u vtable=0x%08X "
            "handle=0x%08X (zeroed 256-bit sig)",
            count_before, stride, vtable, M2_LANDING_CRAFT_HANDLE);

    /* Mirror FUN_00665590: FUN_00649180(&sigRegistry, handle, 0, 0, zeroed-sig). insert-if-absent. */
    ret = ((registry_insert_fn)M2_REGISTRY_INSERT_VA)(map, M2_LANDING_CRAFT_HANDLE, 0, 0, record);

    count_after = *(int*)((char*)map + M2_NAME_REG_COUNT_OFF);
    m2_logf("[spawn_registry] inject-signature: post-insert count=%d ret=%d -> %s",
            count_after, ret,
            count_after > count_before ? "INSERTED (was absent)" : "already present (no-op)");
}

/* ── STAGE B: give the template real component records so the spawn worker clones them ───
 *
 * Stage A leaves the spawned instance hollow (no components). To make it a visible, standable
 * boat, the template handle 0x8000B3C5 must carry real COMP records in the live per-component
 * instance pools; the SecuROM spawn worker then clones them onto each new instance. We add the
 * two that matter for "see it": SceneObject (base entity/placement, stride 0x1c — cloned from the
 * box donor) and Model (a 4-byte mesh-handle, set to the landing-craft mesh 0x592057C4).
 *
 * The insert primitive is the engine's own FUN_0064a600 (pool in EDI; stack args key,record;
 * callee-cleaned), paired with FUN_00665590 to set the component's signature bit — exactly the
 * producer idiom in FUN_004cfed0. FUN_0064a090 (ECX=pool, ESI=key) hashes a handle to a bucket.
 * All writes are gated by template_write so a first run only OBSERVES (locates + logs the donor
 * records and the resolved pools) before we ever mutate a pool. */

/* FUN_0064a090(pool in ECX, key in ESI) -> bucket index in EAX (open-addr linear probe). */
static int pool_probe(void* pool, unsigned key) {
    void* fn = (void*)M2_POOL_PROBE_VA;
    int      bucket;
    unsigned scratch_c;
    unsigned scratch_s;
    __asm__ __volatile__(
        "call *%[fn]\n\t"
        : "=a"(bucket), "=c"(scratch_c), "=S"(scratch_s)
        : "1"(pool), "2"(key), [fn]"r"(fn)
        : "edx", "cc", "memory");
    return bucket;
}

/* FUN_0064a600(EDI=pool; push record; push key; call) -> record ptr in EAX; callee-cleaned. */
static void* pool_insert(void* pool, unsigned key, const void* record) {
    void* fn = (void*)M2_POOL_INSERT_VA;
    void* ret;
    __asm__ __volatile__(
        "pushl %[rec]\n\t"
        "pushl %[k]\n\t"
        "call  *%[fn]\n\t"
        : "=a"(ret)
        : [rec]"r"(record), [k]"r"(key), [fn]"r"(fn), "D"(pool)
        : "ecx", "edx", "esi", "cc", "memory");
    return ret;
}

/* Resolve a component record for `key` in `pool` (NULL if absent). Mirrors FUN_0064a600's slot math. */
static void* pool_lookup(void* pool, unsigned key) {
    unsigned  cap      = *(unsigned*)((char*)pool + 0x08);
    int       bucket;
    unsigned* keytab;
    short     stride;
    unsigned  mask;
    unsigned char shift;
    char**    pages;
    if (cap == 0) return NULL;
    bucket = pool_probe(pool, key);
    if (bucket < 0) return NULL;
    keytab = *(unsigned**)((char*)pool + 0x1c);
    if (!keytab || keytab[bucket] != key) return NULL;
    stride = *(short*)((char*)pool + 0x0c);
    mask   = *(unsigned*)((char*)pool + 0x10) - 1u;
    shift  = *(unsigned char*)((char*)pool + 0x0e);
    pages  = *(char***)((char*)pool + 0x20);
    return pages[(unsigned)bucket >> shift] + (mask & (unsigned)bucket) * (unsigned)stride;
}

/* Validate a live pool object by its structural invariants: the stride must equal the class's known
 * element size (the decisive identity check), records-per-page must be 2^page_shift (the bucket-mask
 * consistency the slot math relies on), the (already grown) capacity must be a whole number of pages,
 * and the key/page tables must be allocated. NOTE: capacity is NOT 2^shift on a live pool — it grows
 * by whole pages as entities are added (SceneObject was live at cap=161280 = 630 pages). Reads the
 * component's signature bit-id (s16 at pool-0x14) out via *bit_out (pool is a runtime param, so this
 * negative offset isn't a constant-folded array-bounds read). */
static int pool_ok(void* pool, const char* label, short expect_stride, short* bit_out) {
    unsigned      cap    = *(unsigned*)((char*)pool + 0x08);
    unsigned char shift  = *(unsigned char*)((char*)pool + 0x0e);
    short         stride = *(short*)((char*)pool + 0x0c);
    unsigned      recspp = *(unsigned*)((char*)pool + 0x10);
    void*         keytab = *(void**)((char*)pool + 0x1c);
    void*         pages  = *(void**)((char*)pool + 0x20);
    short         bit    = *(short*)((char*)pool - 0x14);
    *bit_out = bit;
    if (stride != expect_stride || recspp == 0 || recspp != (1u << shift) ||
        cap == 0 || (cap % recspp) != 0 || !keytab || !pages) {
        m2_logf("[spawn_registry] inject-template: %s pool @0x%08X FAILS self-check "
                "(stride=%d want %d, cap=%u shift=%u recspp=%u keytab=%p pages=%p) — aborted",
                label, (unsigned)(UINT_PTR)pool, stride, expect_stride, cap, shift, recspp, keytab, pages);
        return 0;
    }
    m2_logf("[spawn_registry] inject-template: %s pool @0x%08X ok (stride=%d cap=%u recspp=%u bit_id=%d)",
            label, (unsigned)(UINT_PTR)pool, stride, cap, recspp, bit);
    return 1;
}

typedef void (__cdecl *sig_setbit_fn)(unsigned key, int bit_id);

/* ENUMERATE which pools a handle actually lives in, by walking both global descriptor arrays.
 * Read-only: skips any pool whose descriptor looks malformed (non-power-of-2 page size, null
 * tables). For each hit logs the class type_hash, the global descriptor index (a candidate
 * signature type-id), the stride, and the first bytes of the record. This is how we learn a
 * donor's real live component set instead of guessing. */
static void enum_handle_pools(unsigned handle, const char* who) {
    void*     arrA_base = (void*)M2_DESC_ARRAY_A_VA;
    void*     arrB_base = (void*)M2_DESC_ARRAY_B_VA;
    /* The engine reads both counts as SIGNED 16-bit (movsx word) — read them the same way, or the
     * high word is garbage and the loop runs away. Clamp as a backstop. */
    int       cntA = *(short*)(UINT_PTR)M2_DESC_ARRAY_A_CNT;
    int       cntB = *(short*)(UINT_PTR)M2_DESC_ARRAY_B_CNT;
    int       pass, i, hits = 0;

    if (cntA < 0 || cntA > 512) cntA = 0;
    if (cntB < 0 || cntB > 512) cntB = 0;
    m2_logf("[spawn_registry] enum-pools for %s 0x%08X: %d authored + %d runtime classes",
            who, handle, cntA, cntB);

    for (pass = 0; pass < 2; pass++) {
        unsigned* arr = (unsigned*)(pass == 0 ? arrA_base : arrB_base);
        int       cnt = (pass == 0 ? cntA : cntB);
        for (i = 0; i < cnt; i++) {
            char*    desc = (char*)(UINT_PTR)arr[i];
            unsigned type_hash;
            void*    pool;
            unsigned cap, recspp;
            void*    keytab; void* pages;
            void*    rec;
            if (!desc) continue;
            type_hash = *(unsigned*)(desc + M2_DESC_TYPEHASH_OFF);
            pool      = desc + M2_DESC_POOL_OFF;
            cap       = *(unsigned*)((char*)pool + 0x08);
            recspp    = *(unsigned*)((char*)pool + 0x10);
            keytab    = *(void**)((char*)pool + 0x1c);
            pages     = *(void**)((char*)pool + 0x20);
            /* mult signature 0x9e3779b9 at pool+0x14 is the strong "this is a real component pool"
             * gate — rejects descriptors whose +0x48 isn't a pool of this shape (no runaway hash). */
            if (*(unsigned*)((char*)pool + 0x14) != 0x9E3779B9u) continue;
            if (cap == 0 || recspp == 0 || (recspp & (recspp - 1)) != 0 || !keytab || !pages) continue;
            rec = pool_lookup(pool, handle);
            if (rec) {
                short stride = *(short*)((char*)pool + 0x0c);
                short bit    = *(short*)((char*)pool - 0x14);
                int   gidx   = (pass == 0) ? i : (cntA + i);
                char  hex[3 * 24 + 1];
                int   j, n = 0;
                for (j = 0; j < stride && j < 24; j++) n += wsprintfA(hex + n, "%02X ", ((unsigned char*)rec)[j]);
                m2_logf("[spawn_registry]   HIT type=0x%08X gidx=%d bit@desc=%d pool=0x%08X stride=%d : %s",
                        type_hash, gidx, bit, (unsigned)(UINT_PTR)pool, stride, hex);
                hits++;
            }
        }
    }
    m2_logf("[spawn_registry] enum-pools for %s: %d component(s)", who, hits);
}

static void m2_spawn_registry_inject_template(int reached_idx, void* ud) {
    void*         so_pool    = (void*)M2_SCENEOBJECT_POOL_VA;
    void*         model_pool = (void*)M2_MODEL_POOL_VA;
    void*         box_so;
    void*         box_model;
    unsigned char so_rec[64];
    short         so_stride, so_bit, model_bit;
    char          hex[3 * 32 + 1];
    unsigned      lc_model = M2_LC_MODEL_HASH;
    int           i, n;

    (void)reached_idx;
    (void)ud;

    if (!g_inject_template) {
        m2_logf("[spawn_registry] inject-template disabled via ini — skipped");
        return;
    }

    /* Discover live component sets. box has none (bare placeholder); the INI-supplied handles let us
     * point the enum at a real template (donor candidate) + a known-live entity (enum validation). */
    enum_handle_pools(M2_BOX_HANDLE, "box");
    if (g_enum1) enum_handle_pools(g_enum1, "enum1");
    if (g_enum2) enum_handle_pools(g_enum2, "enum2");
    if (g_enum3) enum_handle_pools(g_enum3, "enum3");

    if (!pool_ok(so_pool, "SceneObject", 0x1c, &so_bit) ||
        !pool_ok(model_pool, "Model", 0x04, &model_bit)) return;

    so_stride = *(short*)((char*)so_pool + 0x0c);

    /* OBSERVE: locate the box donor's live records and log them (proves pool addressing before we write). */
    box_so    = pool_lookup(so_pool, M2_BOX_HANDLE);
    box_model = pool_lookup(model_pool, M2_BOX_HANDLE);
    if (!box_so) {
        m2_logf("[spawn_registry] inject-template: box 0x%08X has NO SceneObject record — aborted",
                M2_BOX_HANDLE);
        return;
    }
    n = 0;
    for (i = 0; i < so_stride && i < 32; i++) n += wsprintfA(hex + n, "%02X ", ((unsigned char*)box_so)[i]);
    m2_logf("[spawn_registry] inject-template: box SceneObject[%d] = %s", so_stride, hex);
    if (box_model) {
        m2_logf("[spawn_registry] inject-template: box Model = 0x%08X",
                *(unsigned*)box_model);
    } else {
        m2_logf("[spawn_registry] inject-template: box has NO Model record (will still inject ours)");
    }

    if (!g_template_write) {
        m2_logf("[spawn_registry] inject-template: OBSERVE ONLY (template_write=0) — no pool mutated. "
                "SceneObject bit_id=%d Model bit_id=%d; set template_write=1 to inject.",
                so_bit, model_bit);
        return;
    }

    /* ACT: clone box's SceneObject under our handle; inject our Model hash; set both signature bits. */
    memset(so_rec, 0, sizeof(so_rec));
    memcpy(so_rec, box_so, (size_t)so_stride);   /* re-read fresh: box_so may move if a pool grows below */
    pool_insert(so_pool, M2_LANDING_CRAFT_HANDLE, so_rec);
    ((sig_setbit_fn)M2_SIG_SETBIT_VA)(M2_LANDING_CRAFT_HANDLE, so_bit);
    m2_logf("[spawn_registry] inject-template: SceneObject inserted for 0x%08X (bit %d)",
            M2_LANDING_CRAFT_HANDLE, so_bit);

    pool_insert(model_pool, M2_LANDING_CRAFT_HANDLE, &lc_model);
    ((sig_setbit_fn)M2_SIG_SETBIT_VA)(M2_LANDING_CRAFT_HANDLE, model_bit);
    m2_logf("[spawn_registry] inject-template: Model=0x%08X inserted for 0x%08X (bit %d)",
            lc_model, M2_LANDING_CRAFT_HANDLE, model_bit);

    /* Verify our records are now resolvable. */
    m2_logf("[spawn_registry] inject-template: verify SceneObject=%s Model=%s",
            pool_lookup(so_pool, M2_LANDING_CRAFT_HANDLE) ? "present" : "MISSING",
            pool_lookup(model_pool, M2_LANDING_CRAFT_HANDLE) ? "present" : "MISSING");
}

static void on_ini_kv(void* ud, const char* key, const char* value) {
    (void)ud;
    if (lstrcmpiA(key, "enabled") == 0)               g_enabled = m2_ini_bool(value);
    else if (lstrcmpiA(key, "probe_insert") == 0)     g_probe_insert = m2_ini_bool(value);
    else if (lstrcmpiA(key, "force_gate") == 0)       g_force_gate = m2_ini_bool(value);
    else if (lstrcmpiA(key, "inject_name") == 0)      g_inject_name = m2_ini_bool(value);
    else if (lstrcmpiA(key, "inject_signature") == 0) g_inject_signature = m2_ini_bool(value);
    else if (lstrcmpiA(key, "inject_template") == 0)  g_inject_template = m2_ini_bool(value);
    else if (lstrcmpiA(key, "template_write") == 0)   g_template_write = m2_ini_bool(value);
    else if (lstrcmpiA(key, "enum1") == 0)            g_enum1 = (unsigned)strtoul(value, NULL, 16);
    else if (lstrcmpiA(key, "enum2") == 0)            g_enum2 = (unsigned)strtoul(value, NULL, 16);
    else if (lstrcmpiA(key, "enum3") == 0)            g_enum3 = (unsigned)strtoul(value, NULL, 16);
    else if (lstrcmpiA(key, "hook_guidmap") == 0)     g_hook_guidmap = m2_ini_bool(value);
    else if (lstrcmpiA(key, "hook_worldentity") == 0) g_hook_worldentity = m2_ini_bool(value);
}

/* Observe-only probe on the registry bucket-insert — the point of this build. */
static void install_insert_probe(void) {
    void* target = (void*)M2_REGISTRY_INSERT_VA;

    if (memcmp(target, k_insert_prologue, sizeof(k_insert_prologue)) != 0) {
        m2_logf("[spawn_registry] insert-probe prologue mismatch at 0x%08X — probe skipped",
                (unsigned)M2_REGISTRY_INSERT_VA);
        return;
    }
    if (!m2_hook_attach(target, (void*)registry_insert_probe, (void**)&g_insert_orig)) {
        m2_logf("[spawn_registry] insert-probe attach failed at 0x%08X — probe skipped",
                (unsigned)M2_REGISTRY_INSERT_VA);
        return;
    }
    m2_logf("[spawn_registry] insert-probe attached at 0x%08X (watching key 0x%08X / 0x%08X)",
            (unsigned)M2_REGISTRY_INSERT_VA, M2_LANDING_CRAFT_HANDLE, M2_LANDING_CRAFT_HASH);
}

/* The old force-create detour on FUN_004CC130. Proven the WRONG gate (never took a gated hit
 * in a full load), so it installs only behind force_gate=1 and is not fired by default. */
static void install_force_gate(void) {
    void* target = (void*)M2_TYPE_SINGLETON_GETCREATE_VA;

    if (memcmp(target, k_prologue, sizeof(k_prologue)) != 0) {
        m2_logf("[spawn_registry] force-gate prologue mismatch — hook skipped");
        return;
    }
    if (!m2_hook_attach(target, (void*)spawn_registry_detour, (void**)&g_orig)) {
        m2_logf("[spawn_registry] force-gate attach failed at 0x%08X — hook skipped",
                (unsigned)M2_TYPE_SINGLETON_GETCREATE_VA);
        return;
    }
    m2_logf("[spawn_registry] force-gate attached at 0x%08X (guidmap=%d worldentity=%d)",
            (unsigned)M2_TYPE_SINGLETON_GETCREATE_VA, g_hook_guidmap, g_hook_worldentity);
}

void m2_spawn_registry_install(void) {
    char inipath[MAX_PATH];

    /* Diagnostics go to m2-sdk.log next to the DLL (this module IS the DLL). */
    m2_log_init(M2_SELF_MODULE);

    m2_module_path(M2_SELF_MODULE, "spawn_registry.ini", inipath, sizeof(inipath));
    m2_ini_parse(inipath, on_ini_kv, NULL);   /* missing file -> defaults */

    if (!g_enabled) {
        m2_logf("[spawn_registry] disabled via ini — nothing installed");
        return;
    }

    if (!m2_hook_init()) {
        m2_logf("[spawn_registry] MinHook init failed — nothing installed");
        return;
    }

    if (g_probe_insert) install_insert_probe();
    else m2_logf("[spawn_registry] insert-probe disabled via ini");

    if (g_force_gate) install_force_gate();
    else m2_logf("[spawn_registry] force-gate disabled via ini (wrong site — probe only)");

    /* The real fix: arm the name-injection to fire once the world is fully loaded (after base
     * has populated the registry). REACHED_WORLD is the stable, unambiguously-after moment.
     * Stage A's signature-injection arms on the same phase (order-independent; both are
     * insert-if-absent). One m2_loadtrigger_install() covers every armed callback. */
    if (g_inject_signature) {
        m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, m2_spawn_registry_inject_signature, NULL);
        m2_logf("[spawn_registry] inject-signature armed for phase %d (world fully loaded)",
                M2_PHASE_REACHED_WORLD_IDX);
    } else {
        m2_logf("[spawn_registry] inject-signature disabled via ini");
    }

    if (g_inject_name) {
        m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, m2_spawn_registry_inject_name, NULL);
        m2_logf("[spawn_registry] inject-name armed for phase %d (world fully loaded)",
                M2_PHASE_REACHED_WORLD_IDX);
    } else {
        m2_logf("[spawn_registry] inject-name disabled via ini");
    }

    /* Stage B arms LAST so the handle already has a signature (Stage A) + name before the template
     * records land and their signature bits are OR'd in. */
    if (g_inject_template) {
        m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, m2_spawn_registry_inject_template, NULL);
        m2_logf("[spawn_registry] inject-template armed for phase %d (write=%d)",
                M2_PHASE_REACHED_WORLD_IDX, g_template_write);
    } else {
        m2_logf("[spawn_registry] inject-template disabled via ini");
    }

    if (g_inject_signature || g_inject_name || g_inject_template) {
        m2_loadtrigger_install();
    }
}
