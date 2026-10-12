/* m2_lua.h — the game's Lua, shared by every mod through m2-sdk.dll.
 *
 * One set of detours for the whole process: the script host's namespace walker, luaB_type,
 * CreateTextWidget and the shared log stub, found in the game image by signature and by luaL_Reg name.
 * Mods add namespaces and callbacks; m2-sdk.dll owns the hooks.
 *
 *     static int Hello(void* L) { m2_lua_push_string(L, "hi", -1); return 1; }
 *     static const m2_lua_reg kLib[] = { { "Hello", Hello }, { NULL, NULL } };
 *
 *     // on a worker thread, not in DllMain: m2_lua_install waits for SecuROM to decrypt the code
 *     if (!m2_lua_install()) m2_logf("m2_lua_install: %s", m2_lua_last_error());
 *     m2_lua_add_namespace("MyMod", kLib);
 *
 * Calling conventions: every m2_lua_* function and every callback is cdecl. A Lua C function is
 * `int f(void* L)`, cdecl, returning how many values it pushed.
 */
#ifndef M2_LUA_H
#define M2_LUA_H

#include "m2_api.h"

typedef int (*m2_lua_cfunction)(void* L);

/* One row of a namespace; the table ends with a NULL name. */
typedef struct m2_lua_reg {
    const char* name;
    m2_lua_cfunction func;
} m2_lua_reg;

/* Called on the game thread for each state a script runs on that has not been seen recently.
 * is_new_cycle is 1 when this process has never seen that state: a fresh state after a completed load
 * is the next load cycle (a main-menu return builds fresh states). */
typedef void (*m2_lua_state_cb)(void* L, int is_new_cycle, void* ud);

/* Called on the game thread from each detoured call on a captured state, except while m2_lua_exec runs
 * on that thread. Very frequent: keep it cheap. */
typedef void (*m2_lua_tick_cb)(void* L, void* ud);

/* Resolve the game's Lua sites (waiting up to 30 s for SecuROM to decrypt the code) and attach the
 * detours. Idempotent. 1 on success; 0 with m2_lua_last_error() set. Never from DllMain. */
M2_API int m2_lua_install(void);

/* The last failure the Lua host recorded, or NULL. Valid until the next failure. Also in m2-sdk.log. */
M2_API const char* m2_lua_last_error(void);

/* Add the global table `name` holding `fns` to every state the game creates (before its scripts run) and
 * every state a script runs on. Both are copied. 0 for a NULL or empty name, a NULL table, a name already
 * added, a row without a function, or more than 32 namespaces or 256 functions. */
M2_API int m2_lua_add_namespace(const char* name, const m2_lua_reg* fns);

/* 0 when the 16 slots are taken or cb is NULL. */
M2_API int m2_lua_on_state(m2_lua_state_cb cb, void* ud);
M2_API int m2_lua_on_tick(m2_lua_tick_cb cb, void* ud);

/* Run `len` bytes of Lua source on L, on top of the frame of the function it is called from, and write
 * the result line to out (NUL-terminated, truncated to cap):
 *   "[ok]\t<v1>\t<v2>..."  "[runtime] <v1>..."  "[compile] \"<message>\""  "[bridge] <why not run>"
 * Returns 0 ok, 1 compile error, 2 runtime error, 3 not run. Chunks up to 1 MiB. Call it on the game
 * thread with the L of a state/tick callback or of a running Lua C function. */
M2_API int m2_lua_exec(void* L, const char* chunk, int len, char* out, int cap);

/* 1 when L is a usable lua_State right now (the check m2_lua_exec makes before running), else 0. A
 * tick's L can be a state that has since ended: check it before taking work off a queue for it. */
M2_API int m2_lua_state_ok(void* L);

/* 1 while m2_lua_exec runs on this thread. */
M2_API int m2_lua_in_exec(void);

/* Raise a Lua error with msg from inside a Lua C function; does not return (longjmp to the enclosing
 * pcall), so hold nothing that needs releasing. */
M2_API void m2_lua_raise(void* L, const char* msg);

/* Push one value from inside a Lua C function. Numbers are the game's 32-bit float lua_Number. Strings
 * are interned, so == compares them with any other string of the same bytes; len < 0 takes strlen. */
M2_API void m2_lua_push_nil(void* L);
M2_API void m2_lua_push_bool(void* L, int b);
M2_API void m2_lua_push_number(void* L, double n);
M2_API void m2_lua_push_string(void* L, const char* s, int len);

#endif /* M2_LUA_H */
