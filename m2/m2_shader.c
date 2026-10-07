/* m2_shader.c — the shader registry's Windows side: signature checks, the detour on
 * FUN_0084f130, and the engine calls that build and register each queued record.
 * Validation, the queue and the admission decision live in m2_shader_core.c.
 */
#include "m2_shader.h"
#include "m2_shader_core.h"
#include "m2_hook.h"
#include "m2_log.h"
#include "m2_target.h"
#include <string.h>

/* FUN_0084f130: __thiscall(renderer), one stack argument, `ret 4`. Ghidra types it void; the
 * detour hands back EAX unchanged so nothing the caller might read is lost. */
typedef uint32_t(__attribute__((thiscall)) * RegisterAllFn)(void* renderer, uint32_t arg);
/* FUN_0085ace0: __thiscall(record), `ret`. */
typedef void(__attribute__((thiscall)) * PsBaseCtorFn)(void* record);
/* FUN_0085ac90: __thiscall(record, name, sho, class), `ret 0xc`. */
typedef void(__attribute__((thiscall)) * RegisterFn)(void* record, const char* name,
                                                     const char* sho, int cls);

static RegisterAllFn g_origRegisterAll = NULL;   /* MinHook trampoline */
static volatile LONG g_hooked = 0;
static volatile LONG g_passStarted = 0;

/* Records live for the process, as retail's static records do. Every record the registry builds
 * takes one pool slot, so the pool capacities bound the storage. */
#define M2_SHADER_RECORD_ALIGN 16u
#define M2_SHADER_ALIGNED(n) (((n) + M2_SHADER_RECORD_ALIGN - 1) & ~(M2_SHADER_RECORD_ALIGN - 1))
#define M2_SHADER_ARENA_SIZE                                                  \
    (M2_SHADER_PS_CAPACITY * M2_SHADER_ALIGNED(M2_SHADER_PS_RECORD_MAX) +      \
     M2_SHADER_VS_CAPACITY * M2_SHADER_ALIGNED(M2_SHADER_VS_RECORD_MAX))

static unsigned char g_arena[M2_SHADER_ARENA_SIZE] __attribute__((aligned(16)));
static size_t g_arenaUsed = 0;

static const unsigned char kRegisterAllPrologue[] = M2_SHADER_REGISTER_ALL_PROLOGUE;
static const unsigned char kRegisterPrologue[]    = M2_SHADER_REGISTER_PROLOGUE;
static const unsigned char kPsBaseCtorPrologue[]  = M2_SHADER_PS_BASE_CTOR_PROLOGUE;
static const unsigned char kVsBaseCtorPrologue[]  = M2_SHADER_VS_BASE_CTOR_PROLOGUE;

static int ProloguesMatch(void) {
    return memcmp((const void*)M2_SHADER_REGISTER_ALL_VA, kRegisterAllPrologue,
                  M2_SHADER_PROLOGUE_LEN) == 0 &&
           memcmp((const void*)M2_SHADER_REGISTER_VA, kRegisterPrologue,
                  M2_SHADER_PROLOGUE_LEN) == 0 &&
           memcmp((const void*)M2_SHADER_PS_BASE_CTOR_VA, kPsBaseCtorPrologue,
                  M2_SHADER_PROLOGUE_LEN) == 0 &&
           memcmp((const void*)M2_SHADER_VS_BASE_CTOR_VA, kVsBaseCtorPrologue,
                  M2_SHADER_PROLOGUE_LEN) == 0;
}

static int RegistryStarted(void) {
    return g_passStarted ||
           *(volatile const uint32_t*)M2_SHADER_PS_COUNT_VA != 0 ||
           *(volatile const uint32_t*)M2_SHADER_VS_COUNT_VA != 0;
}

/* FUN_0085ade0 takes the record in EAX, writes through it, and clobbers ECX. */
static void CallVsBaseCtor(void* record) {
    void* fn = (void*)M2_SHADER_VS_BASE_CTOR_VA;
    __asm__ volatile("call *%[fn]"
                     : "+a"(record)
                     : [fn] "r"(fn)
                     : "ecx", "edx", "memory", "cc");
}

static void* NewRecord(const m2_shader_family_info* info) {
    size_t size = M2_SHADER_ALIGNED((size_t)info->size);
    void* record;
    if (sizeof(g_arena) - g_arenaUsed < size) return NULL;
    record = g_arena + g_arenaUsed;
    g_arenaUsed += size;
    memset(record, 0, info->size);
    if (info->stage == M2_SHADER_STAGE_PIXEL)
        ((PsBaseCtorFn)M2_SHADER_PS_BASE_CTOR_VA)(record);
    else
        CallVsBaseCtor(record);
    *(uint32_t*)record = info->vtable;
    return record;
}

static void RegisterEntry(m2_shader_entry* e, uint8_t shader_level) {
    const m2_shader_family_info* info = m2_shader_core_family((int)e->family);
    int pixel = info->stage == M2_SHADER_STAGE_PIXEL;
    uint32_t live = *(volatile const uint32_t*)(pixel ? M2_SHADER_PS_COUNT_VA : M2_SHADER_VS_COUNT_VA);
    const uint32_t* keys = (const uint32_t*)(pixel ? M2_SHADER_PS_KEYS_VA : M2_SHADER_VS_KEYS_VA);
    uint32_t slots = pixel ? M2_SHADER_PS_CAPACITY : M2_SHADER_VS_CAPACITY;
    unsigned n = m2_shader_core_registrations(e, shader_level);
    void* records[M2_SHADER_PIXEL_CLASSES];
    m2_shader_status st = m2_shader_core_admit(e, shader_level, live, keys, slots);
    unsigned c;

    for (c = 0; st == M2_SHADER_OK && c < n; c++) {
        records[c] = NewRecord(info);
        if (!records[c]) st = M2_SHADER_ERR_CAPACITY;
    }
    if (st == M2_SHADER_OK) {
        for (c = 0; c < n; c++) {
            ((RegisterFn)M2_SHADER_REGISTER_VA)(records[c], e->cls[c].name, e->cls[c].sho, (int)c);
        }
    }
    m2_shader_core_record(e, st);
    if (st != M2_SHADER_OK) {
        m2_logf_mod((HMODULE)e->owner, "m2_shader: %s entry \"%s\" (%s) not registered: %s",
                    pixel ? "pixel" : "vertex", e->cls[0].name, info->name,
                    m2_shader_core_status_name(st));
    }
}

static uint32_t __attribute__((thiscall)) Detour_RegisterAll(void* renderer, uint32_t arg) {
    uint32_t ret;
    unsigned i, count;
    uint8_t shader_level;

    InterlockedExchange(&g_passStarted, 1);
    ret = g_origRegisterAll(renderer, arg);

    /* Read after the game's own pass, which reads it too. */
    shader_level = *(volatile const uint8_t*)M2_SHADER_LEVEL_VA;
    count = m2_shader_core_entry_count();
    for (i = 0; i < count; i++) RegisterEntry(m2_shader_core_entry(i), shader_level);
    return ret;
}

/* The detour's prologue check reads the original bytes, so it runs before MinHook patches them;
 * once hooked, only the registry state is re-checked. */
static m2_shader_status EnsureInstalled(void) {
    if (g_hooked) return RegistryStarted() ? M2_SHADER_ERR_TOO_LATE : M2_SHADER_OK;
    if (!ProloguesMatch()) return M2_SHADER_ERR_SIGNATURE;
    if (RegistryStarted()) return M2_SHADER_ERR_TOO_LATE;
    if (!m2_hook_attach((void*)M2_SHADER_REGISTER_ALL_VA, (void*)Detour_RegisterAll,
                        (void**)&g_origRegisterAll))
        return M2_SHADER_ERR_HOOK;
    InterlockedExchange(&g_hooked, 1);
    return M2_SHADER_OK;
}

/* The module that holds `addr` — the caller of the add function — so a failed entry's log line
 * lands in that mod's own log. */
static HMODULE ModuleAt(void* addr) {
    HMODULE module = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)addr, &module);
    return module;
}

m2_shader_status m2_shader_add_pixel(m2_shader_family family, const m2_shader_class classes[4]) {
    HMODULE owner = ModuleAt(__builtin_return_address(0));
    m2_shader_status st = EnsureInstalled();
    if (st != M2_SHADER_OK) return st;
    return m2_shader_core_queue_pixel(family, classes, (void*)owner);
}

m2_shader_status m2_shader_add_vertex(m2_shader_family family, const m2_shader_class* cls) {
    HMODULE owner = ModuleAt(__builtin_return_address(0));
    m2_shader_status st = EnsureInstalled();
    if (st != M2_SHADER_OK) return st;
    return m2_shader_core_queue_vertex(family, cls, (void*)owner);
}

m2_shader_status m2_shader_outcome(const char* name) {
    return m2_shader_core_outcome(name);
}

const char* m2_shader_status_name(m2_shader_status s) {
    return m2_shader_core_status_name(s);
}
