/* m2_worldentity.c — general worldentity template registry (see m2_worldentity.h).
 *
 * BUILD STATE (incremental, matching the proven-first discipline):
 *  - Milestone 1 (THIS): the general abstraction + registry + load-hook, and per template it injects
 *    the PROVEN pieces — the name->handle record (name registry 0xDF6B88) and a zeroed component
 *    signature (0xDF6D08). Result: every registered name resolves via Pg.GetGuidByName and Pg.Spawn
 *    completes without crashing (a hollow instance). This is the composable foundation.
 *  - Milestone 2 (NEXT): drive the game's own COMP loader FUN_00654940 over an authored additive
 *    block synthesized from each template's `comps`, so the engine mints the dense index, inserts the
 *    reflection records + a REAL signature (bits for the populated comps), and instantiates the
 *    entity — the non-hollow, visible result. The `comps` are already captured here for that pass.
 *
 * Injection safety mirrors m2_spawn_registry.c: MinGW has no SEH, so we pre-validate (registry
 * vtable identity, the inserter's prologue signature, element stride bounds) before the insert.
 */
#include "m2_worldentity.h"
#include "m2_target.h"
#include "m2_log.h"
#include "m2_hook.h"
#include "m2_ini.h"
#include "m2_loadtrigger.h"

#include <windows.h>
#include <string.h>
#include <limits.h>

/* ── pandemic_hash_m2: name -> 32-bit asset hash ─────────────────────────────────────────
 * FNV-1a (basis 0x811C9DC5, prime 0x01000193) with a per-byte |0x20 case-fold, finalized with
 * ^0x2A then one more *prime. Empty string hashes to 0. Ported from mercs2_formats::hash. */
static unsigned m2_hash_m2(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    unsigned h;
    if (!s || !*s) return 0u;
    h = 0x811C9DC5u;
    for (; *p; ++p) {
        h ^= (unsigned)(*p | 0x20);
        h *= 0x01000193u;
    }
    h ^= 0x2Au;
    h *= 0x01000193u;
    return h;
}

/* Registry bucket-insert FUN_00649180 — __stdcall(int* table, key, aux, key2, val) -> int. */
typedef int(__stdcall* registry_insert_fn)(int* table, unsigned key, unsigned aux, int key2,
                                           void* val);

/* First 16 bytes of FUN_00649180 for the running build — refuse to insert on any mismatch. */
static const unsigned char k_insert_prologue[16] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x53, 0x8B,
    0x5D, 0x08, 0x56, 0x57, 0x83, 0xCF, 0xFF, 0xF6
};

/* ── The mutable template set ────────────────────────────────────────────────────────────
 * Fresh handles are assigned from a base (the first free 0x8000xxxx slot) + index. The base is a
 * measured-free constant for now; Milestone 2 resolves it live from the worldentity so it is robust
 * across base builds. */
#define M2_WE_MAX          64
#define M2_WE_HANDLE_BASE  0x8000B3C5u

typedef struct {
    const char*          name;
    const m2_comp_rec_t* comps;
    size_t               n_comps;
    unsigned             handle;   /* assigned at install/load */
} m2_we_template_t;

static m2_we_template_t g_templates[M2_WE_MAX];
static LONG             g_count      = 0;
static LONG             g_installed  = 0;

int m2_worldentity_register_template(const char* name, const m2_comp_rec_t* comps, size_t n_comps) {
    LONG slot;
    if (!name || !*name) return 0;
    slot = InterlockedIncrement(&g_count) - 1;   /* claim a slot lock-free (DllMain has no locks) */
    if (slot >= M2_WE_MAX) {
        InterlockedDecrement(&g_count);
        return 0;
    }
    g_templates[slot].name    = name;
    g_templates[slot].comps   = comps;
    g_templates[slot].n_comps = n_comps;
    g_templates[slot].handle  = 0;
    return 1;
}

/* Validate one of the two keyed registries (name / signature) and return its stride, or 0 on any
 * failure. `expect_vtable` != 0 → the vtable at *table must equal it; == 0 → it must merely point
 * into .rdata (the signature registry, whose exact vtable we don't hard-code). */
static unsigned reg_ok(int* table, unsigned expect_vtable, const char* label) {
    unsigned vtable = *(unsigned*)table;
    unsigned stride;
    if (expect_vtable) {
        if (vtable != expect_vtable) {
            m2_logf("[worldentity] %s: registry vtable 0x%08X != 0x%08X — aborted", label, vtable,
                    expect_vtable);
            return 0;
        }
    } else if (vtable < M2_RDATA_START_VA || vtable >= M2_RDATA_START_VA + M2_RDATA_SIZE) {
        m2_logf("[worldentity] %s: registry vtable 0x%08X not in .rdata — aborted", label, vtable);
        return 0;
    }
    if (memcmp((void*)M2_REGISTRY_INSERT_VA, k_insert_prologue, sizeof(k_insert_prologue)) != 0) {
        m2_logf("[worldentity] %s: inserter prologue mismatch — aborted", label);
        return 0;
    }
    stride = *(unsigned short*)((char*)table + M2_NAME_REG_STRIDE_OFF);
    if (stride < 4 || stride > 64) {
        m2_logf("[worldentity] %s: unexpected element stride %u — aborted", label, stride);
        return 0;
    }
    return stride;
}

/* Milestone-1 injection for one template: name->handle + a zeroed 256-bit signature. */
static void inject_template_m1(m2_we_template_t* t) {
    int*           name_reg = (int*)M2_REGISTRY_NAME_TABLE_VA;
    int*           sig_reg  = (int*)M2_SIG_REG_TABLE_VA;
    unsigned       name_hash = m2_hash_m2(t->name);
    unsigned char  record[64];
    int            before, after, ret;

    if (!reg_ok(name_reg, M2_NAME_REG_VTABLE_VA, "inject-name")) return;
    if (!reg_ok(sig_reg, 0, "inject-signature")) return;

    /* Name record: field0 = char* name (stored by reference), then handle + name-hash. */
    memset(record, 0, sizeof(record));
    *(const char**)(record + 0) = t->name;
    *(unsigned*)(record + 4)    = t->handle;
    *(unsigned*)(record + 8)    = name_hash;
    *(unsigned*)(record + 12)   = name_hash;
    before = *(int*)((char*)name_reg + M2_NAME_REG_COUNT_OFF);
    ret = ((registry_insert_fn)M2_REGISTRY_INSERT_VA)(name_reg, t->handle, name_hash,
                                                      (int)name_hash, record);
    after = *(int*)((char*)name_reg + M2_NAME_REG_COUNT_OFF);
    m2_logf("[worldentity] '%s' handle=0x%08X hash=0x%08X: name %d->%d ret=%d %s", t->name,
            t->handle, name_hash, before, after, ret,
            after > before ? "INSERTED" : "present(no-op)");

    /* Zeroed 256-bit component signature (all bits clear = notify no systems; safe but hollow). */
    memset(record, 0, sizeof(record));
    before = *(int*)((char*)sig_reg + M2_NAME_REG_COUNT_OFF);
    ((registry_insert_fn)M2_REGISTRY_INSERT_VA)(sig_reg, t->handle, 0, 0, record);
    after = *(int*)((char*)sig_reg + M2_NAME_REG_COUNT_OFF);
    m2_logf("[worldentity] '%s' signature(zeroed): sig %d->%d %s", t->name, before, after,
            after > before ? "INSERTED" : "present(no-op)");
    /* Milestone 2 will inject t->comps + a real signature here (FUN_00654940 re-drive). */
}

/* ── M2 Milestone 2a: OBSERVE the real COMP-loader context ───────────────────────────────────
 * Before authoring the replay context by hand, pin what the engine's own context-builder
 * (`thunk_FUN_024ea7e0`) produces for the base worldentity load: the ~68-byte context header
 * (esp. the fields FUN_00464780 doesn't touch — +0/+4/+0x14) and the inline reader's vtable. This
 * is READ-ONLY (log + call original); it changes nothing. Gated behind `observe_loader=1` (a
 * diagnostic — the standing "no flags" mandate excepts diagnostics). It identifies the worldentity
 * load precisely by its opening chunk fourccs CHDR·enum·UNIQ, and dumps that one context once. */
typedef unsigned(__cdecl* comp_loader_fn)(void* ctx, int flag);
static comp_loader_fn g_loader_orig = 0;
static int            g_observe_loader = 0;
static LONG           g_obs_calls = 0;
static LONG           g_obs_dumped = 0;

static unsigned rd32(const void* p) {
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

static void dump_hex(const char* label, const unsigned char* p, int n) {
    char line[160];
    int i, o = 0;
    o += wsprintfA(line + o, "[worldentity] OBSERVE %s:", label);
    for (i = 0; i < n && o < (int)sizeof(line) - 4; ++i) {
        o += wsprintfA(line + o, " %02X", p[i]);
    }
    m2_logf("%s", line);
}

static unsigned __cdecl we_loader_observe(void* ctx, int flag) {
    LONG call = InterlockedIncrement(&g_obs_calls);
    if (ctx) {
        unsigned count = rd32((char*)ctx + M2_CTX_COUNT_OFF);
        unsigned chunkarr = rd32((char*)ctx + M2_CTX_CHUNKARR_OFF);
        unsigned fc0 = chunkarr ? rd32((void*)(size_t)chunkarr) : 0;
        /* The worldentity master container is the big one (count=786); the chunk-array is the block
         * itself so chunk[0].fourcc='UCFX' and chunk[1]='CHDR'. Dump its full context once. */
        if (count >= 100 && fc0 == 0x58464355u /*'UCFX'*/
            && InterlockedExchange(&g_obs_dumped, 1) == 0) {
            unsigned reader_vtbl = rd32((char*)ctx + M2_CTX_READER_OFF);
            unsigned data_base = rd32((char*)ctx + M2_CTX_DATABASE_OFF);
            m2_logf("[worldentity] OBSERVE *** worldentity ctx: count=%u data_base=0x%08X "
                    "chunkarr=0x%08X reader_vtable=0x%08X (call #%ld)",
                    count, data_base, chunkarr, reader_vtbl, (long)call);
            dump_hex("ctx[0..0x44]", (const unsigned char*)ctx, 0x44);
            /* the reader's own 0x24 bytes (ctx+0x18) + a few of its vtable slots */
            if (reader_vtbl > 0x400000 && reader_vtbl < 0x1000000) {
                m2_logf("[worldentity] OBSERVE  reader.vtable[0x14]=0x%08X [0x28]=0x%08X",
                        rd32((void*)(size_t)(reader_vtbl + 0x14)), rd32((void*)(size_t)(reader_vtbl + 0x28)));
            }
            /* chunk rows 0..4: {fourcc,row_off,size,w3,w4} — row0='UCFX' header, row1='CHDR', ... */
            int r;
            for (r = 0; r < 5 && r < (int)count; ++r) {
                const unsigned char* row = (const unsigned char*)(size_t)(chunkarr + r * 0x14);
                m2_logf("[worldentity] OBSERVE  chunk[%d] fourcc=0x%08X row_off=%u size=%u w3=%u w4=%u",
                        r, rd32(row), rd32(row + 4), rd32(row + 8), rd32(row + 0xc), rd32(row + 0x10));
            }
        }
        /* fallback trace: the first dozen calls' shape, so a windowed (non-CHDR-first) context is
         * still visible if the precise filter never matches. */
        if (call <= 12) {
            m2_logf("[worldentity] OBSERVE call #%ld count=%u chunkarr=0x%08X fc0=0x%08X",
                    (long)call, count, chunkarr, fc0);
        }
    }
    return g_loader_orig(ctx, flag);
}

/* ── M2 Milestone 2b: the post-load replay ───────────────────────────────────────────────────
 * Build the ~68-byte load context over an authored .wetb block (chunk-array = the block itself,
 * count = data_area_off/20, data_base = block+data_area_off) + a self-contained reader shim, and call
 * the engine's own COMP loader `FUN_00654940(ctx,0)` at world-load. The loader mints the new handle's
 * dense index, inserts its reflection records, reads its flgs signature into 0xDF6D08, and
 * instantiates it — additively (base's records untouched). Faithful to the live-observed layout and
 * the reader methods 0x00825EC0 (read) / 0x00825E90 (seek). Gated behind `replay=1`. */
static int   g_replay = 0;
static char  g_block_path[MAX_PATH] = {0};   /* .wetb; default resolved next to the DLL */

/* Reader shim — the loader invokes exactly four reader-vtable methods (enumerated from the full
 * FUN_00654940 / FUN_00464780 disasm): [0x14] read, [0x1c] seek-abs, [0x20] tell, [0x28] seek-flag.
 * All operate only on the memory-stream fields the engine's FUN_00825e40 sets (relative to the reader
 * at ctx+0x18): length@+8, length_hi@+0xc, cursor@+0x10, carry@+0x14, base@+0x18, flags@+0x20 — NONE
 * touch reader+4 (a runtime sysptr in the real object), which is why a self-contained shim is safe.
 * Other slots are inert stubs (never reached). Each mirrors its engine original byte-for-byte:
 *   read     0x00825EC0   seek-abs 0x00825FB0   tell 0x0069F850   seek-flag 0x00825E90 */
static void __attribute__((thiscall)) shim_seekflag(void* self) {
    ((unsigned char*)self)[0x20] &= 0xFEu;   /* flags &= ~1  (0x00825E90) */
}
static int __attribute__((thiscall)) shim_read(void* self, void* dest, unsigned n, int flag) {
    unsigned char* r = (unsigned char*)self;
    if (dest) {                              /* dest==0 => no-op, cursor NOT advanced (as 0x00825EC0) */
        unsigned cursor, carry, base, newc;
        memcpy(&cursor, r + 0x10, 4);
        memcpy(&carry, r + 0x14, 4);
        memcpy(&base, r + 0x18, 4);
        newc = cursor + n;
        carry += (unsigned)flag + (newc < cursor ? 1u : 0u);   /* adc pattern */
        memcpy(r + 0x10, &newc, 4);
        memcpy(r + 0x14, &carry, 4);
        memcpy(dest, (const void*)(size_t)(base + cursor), n);
    }
    return 0;
}
/* tell (0x0069F850): return the 64-bit position — eax=cursor(+0x10), edx=carry(+0x14). */
static long long __attribute__((thiscall)) shim_tell(void* self) {
    unsigned char* r = (unsigned char*)self;
    unsigned cursor, carry;
    memcpy(&cursor, r + 0x10, 4);
    memcpy(&carry, r + 0x14, 4);
    return ((long long)(unsigned long long)carry << 32) | (unsigned long long)cursor;
}
/* seek-abs (0x00825FB0): whence 1=SET (cursor=off), 0=CUR (cursor+=off), 2=END (cursor=length+off). */
static void __attribute__((thiscall)) shim_seekabs(void* self, unsigned off_lo, unsigned off_hi, int whence) {
    unsigned char* r = (unsigned char*)self;
    unsigned cursor, carry, len, len_hi, base_lo, newc;
    memcpy(&cursor, r + 0x10, 4);
    memcpy(&carry, r + 0x14, 4);
    if (whence == 1) {                       /* SET */
        memcpy(r + 0x10, &off_lo, 4);
        memcpy(r + 0x14, &off_hi, 4);
    } else if (whence == 0) {                /* CUR */
        newc = cursor + off_lo;
        carry += off_hi + (newc < cursor ? 1u : 0u);
        memcpy(r + 0x10, &newc, 4);
        memcpy(r + 0x14, &carry, 4);
    } else if (whence == 2) {                /* END */
        memcpy(&len, r + 0x08, 4);
        memcpy(&len_hi, r + 0x0c, 4);
        base_lo = len + off_lo;
        len_hi = len_hi + off_hi + (base_lo < len ? 1u : 0u);
        memcpy(r + 0x10, &base_lo, 4);
        memcpy(r + 0x14, &len_hi, 4);
    }
}
static void __attribute__((thiscall)) shim_stub(void* self) { (void)self; }
static void* g_shim_vtable[12];
static void shim_vtable_init(void) {
    int i;
    for (i = 0; i < 12; ++i) g_shim_vtable[i] = (void*)shim_stub;
    g_shim_vtable[5]  = (void*)shim_read;      /* +0x14 read     */
    g_shim_vtable[7]  = (void*)shim_seekabs;   /* +0x1c seek-abs */
    g_shim_vtable[8]  = (void*)shim_tell;      /* +0x20 tell     */
    g_shim_vtable[10] = (void*)shim_seekflag;  /* +0x28 seek-flag*/
}

/* Load g_block_path into a committed RW buffer (kept alive: the loader may store the Name string by
 * reference). Returns the base pointer + size, or NULL. */
static unsigned char* load_block(unsigned* out_len) {
    HANDLE h = CreateFileA(g_block_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD sz, got = 0;
    unsigned char* buf;
    if (h == INVALID_HANDLE_VALUE) {
        m2_logf("[worldentity] REPLAY: cannot open block '%s' (err %lu)", g_block_path, GetLastError());
        return NULL;
    }
    sz = GetFileSize(h, NULL);
    buf = (unsigned char*)VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf || !ReadFile(h, buf, sz, &got, NULL) || got != sz) {
        m2_logf("[worldentity] REPLAY: read failed (%lu/%lu)", got, sz);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    *out_len = sz;
    return buf;
}

static void do_replay(void) {
    unsigned len = 0, data_area_off, ndesc, count, handle;
    unsigned char* block = load_block(&len);
    unsigned char ctx[0x48];
    unsigned loader_ret;
    int sig_before, sig_after;
    if (!block || len < 24 || memcmp(block, "UCFX", 4) != 0) {
        m2_logf("[worldentity] REPLAY: bad/empty block");
        return;
    }
    memcpy(&data_area_off, block + 4, 4);
    memcpy(&ndesc, block + 16, 4);
    if (data_area_off % 20 != 0 || (unsigned)(20 + ndesc * 20) != data_area_off) {
        m2_logf("[worldentity] REPLAY: block header inconsistent (dao=%u ndesc=%u)", data_area_off, ndesc);
        return;
    }
    count = data_area_off / 20;                 /* = 1 (UCFX pseudo-row) + ndesc */

    shim_vtable_init();
    memset(ctx, 0, sizeof(ctx));
    *(unsigned*)(ctx + 0x00) = (unsigned)(size_t)block;                   /* chunk_array (= block) */
    *(unsigned*)(ctx + 0x08) = count;                                     /* COUNT */
    *(unsigned*)(ctx + 0x0c) = (unsigned)(size_t)(block + data_area_off); /* DATA_BASE */
    *(unsigned*)(ctx + 0x10) = (unsigned)(size_t)block;                   /* chunk_array (mirror) */
    *(unsigned*)(ctx + 0x18) = (unsigned)(size_t)g_shim_vtable;           /* reader.vtable */

    /* recover the handle from the block's UNIQ chunk ([u32 1][u32 handle]) for the log/read-back */
    handle = 0;
    {
        unsigned r;
        for (r = 1; r < count; ++r) {
            unsigned fourcc, row_off;
            memcpy(&fourcc, block + r * 20, 4);
            memcpy(&row_off, block + r * 20 + 4, 4);
            if (fourcc == 0x51494e55u) { memcpy(&handle, block + data_area_off + row_off + 4, 4); break; }
        }
    }

    sig_before = *(int*)(size_t)(M2_SIG_REG_TABLE_VA + M2_NAME_REG_COUNT_OFF);
    m2_logf("[worldentity] REPLAY: block %u B, count=%u, handle=0x%08X, data_base=0x%08X, thread=%lu — calling loader 0x%08X",
            len, count, handle, (unsigned)(size_t)(block + data_area_off), GetCurrentThreadId(), M2_COMP_LOADER_VA);
    loader_ret = ((comp_loader_fn)(size_t)M2_COMP_LOADER_VA)(ctx, 0);
    m2_logf("[worldentity] REPLAY: loader RETURNED %u (did not hang)", loader_ret);   /* separate: proves return */
    sig_after = *(int*)(size_t)(M2_SIG_REG_TABLE_VA + M2_NAME_REG_COUNT_OFF);
    m2_logf("[worldentity] REPLAY: sig-reg count %d -> %d (%s)",
            sig_before, sig_after,
            sig_after > sig_before ? "INSERTED" : "no change");
}

static void on_ini_kv(void* ud, const char* key, const char* value) {
    (void)ud;
    if (lstrcmpiA(key, "observe_loader") == 0)      g_observe_loader = m2_ini_bool(value);
    else if (lstrcmpiA(key, "replay") == 0)         g_replay = m2_ini_bool(value);
    else if (lstrcmpiA(key, "template_block") == 0) lstrcpynA(g_block_path, value, sizeof(g_block_path));
}

static void on_world_loaded(int reached_idx, void* ud) {
    LONG n = g_count, i;
    (void)reached_idx;
    (void)ud;
    if (g_replay) {
        /* M2 Milestone 2b: drive the engine's COMP loader over the authored .wetb block. */
        do_replay();
        return;
    }
    if (n > M2_WE_MAX) n = M2_WE_MAX;
    if (n == 0) {
        m2_logf("[worldentity] no templates registered — nothing to inject");
        return;
    }
    m2_logf("[worldentity] injecting %ld registered template(s) at world-load", (long)n);
    for (i = 0; i < n; ++i) {
        g_templates[i].handle = M2_WE_HANDLE_BASE + (unsigned)i;   /* TODO(M2): resolve max live */
        inject_template_m1(&g_templates[i]);
    }
}

void m2_worldentity_install(void) {
    if (InterlockedExchange(&g_installed, 1) != 0) return;   /* idempotent across consumers */
    char inipath[MAX_PATH];
    m2_log_init(M2_SELF_MODULE);
    m2_module_path(M2_SELF_MODULE, "worldentity.ini", inipath, sizeof(inipath));
    m2_ini_parse(inipath, on_ini_kv, NULL);   /* missing file -> defaults (observe off) */
    if (!m2_hook_init()) {
        m2_logf("[worldentity] MinHook init failed — nothing armed");
        return;
    }
    if (g_observe_loader) {
        /* M2 Milestone 2a: pure observe of the base worldentity load context — no injection. */
        if (m2_hook_attach((void*)M2_COMP_LOADER_VA, (void*)we_loader_observe, (void**)&g_loader_orig)) {
            m2_logf("[worldentity] OBSERVE armed: hooked COMP loader 0x%08X (observe_loader=1)", M2_COMP_LOADER_VA);
        } else {
            m2_logf("[worldentity] OBSERVE: failed to hook COMP loader 0x%08X", M2_COMP_LOADER_VA);
        }
        return;
    }
    if (g_replay) {
        /* resolve a bare filename / empty path against the DLL's own directory */
        if (g_block_path[0] == '\0') {
            m2_module_path(M2_SELF_MODULE, "landing_craft.wetb", g_block_path, sizeof(g_block_path));
        } else if (!strchr(g_block_path, '\\') && !strchr(g_block_path, ':')) {
            char just[MAX_PATH];
            lstrcpynA(just, g_block_path, sizeof(just));
            m2_module_path(M2_SELF_MODULE, just, g_block_path, sizeof(g_block_path));
        }
        m2_logf("[worldentity] REPLAY armed for phase %d — block '%s'", M2_PHASE_REACHED_WORLD_IDX, g_block_path);
    }
    m2_loadtrigger_on_phase(M2_PHASE_REACHED_WORLD_IDX, on_world_loaded, NULL);
    m2_loadtrigger_install();
    m2_logf("[worldentity] armed for phase %d (world fully loaded)", M2_PHASE_REACHED_WORLD_IDX);
}
