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

/* Called on the game thread from each detoured call on a captured state, except while m2_lua_exec or
 * m2_lua_pcall runs on that thread. Very frequent: keep it cheap. */
typedef void (*m2_lua_tick_cb)(void* L, void* ud);

/* Resolve the game's Lua sites (waiting up to 30 s for SecuROM to decrypt the code) and attach the
 * detours. Idempotent. 1 on success; 0 with m2_lua_last_error() set. Never from DllMain. */
M2_API int m2_lua_install(void);

/* The last failure the Lua host recorded on the calling thread, or NULL. Valid until that thread's next
 * failure; another thread's failure never touches it (since 0.5.1). Also in m2-sdk.log. */
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

/* 1 while m2_lua_exec or m2_lua_pcall runs on this thread. */
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

/* Function references (since 0.7.0): keep a Lua value, such as a callback a script passes to a binding,
 * and call it later on the game thread. Through the game's own luaL_ref, luaL_unref, lua_rawgeti and
 * lua_pcall, found in the game image by signature. Lua 5.1 semantics. A ref is a slot in the registry of
 * the global state its L belongs to (L+0x10), so it is valid on that L and on every coroutine of it, and
 * the game's own callbacks share that registry. A load cycle builds a fresh global state whose registry
 * numbers its refs from 1 again: never push, call or release a ref made in an earlier cycle, which would
 * reach whatever that number names now. Use the L your binding or tick was handed, never a stored one.
 *
 *     static int g_ref;
 *     static int SetCallback(void* L) { g_ref = m2_lua_ref(L, 1); return 0; }  // MyMod.SetCallback(fn)
 *
 *     // later, on the game thread (a tick callback, after m2_lua_state_ok(L)):
 *     m2_lua_push_ref(L, g_ref);
 *     m2_lua_push_number(L, 42);
 *     if (m2_lua_pcall(L, 1, 0) != 0) m2_logf("callback failed: %s", m2_lua_last_error());
 *     m2_lua_unref(L, g_ref);
 *
 * Every failure is written to m2-sdk.log and kept for m2_lua_last_error(). */

/* Keep the value at Lua stack index idx of L in the registry: luaL_ref(L, LUA_REGISTRYINDEX) on a copy,
 * so the stack is unchanged. idx is a Lua stack index, not m2_lua_arg_*'s 0-based argument index: 1 is
 * the first value of the running function's frame (its first argument), -1 the top. Returns the ref
 * (> 0), or 0 for a nil value, an index naming no value (0, a pseudo-index, outside the frame), an
 * unusable L, or before m2_lua_install has resolved the game's Lua. */
M2_API int m2_lua_ref(void* L, int idx);

/* Release ref: luaL_unref(L, LUA_REGISTRYINDEX, ref); the registry reuses its slot. Once per ref: a
 * second release corrupts the registry's free list. ref <= 0 is refused. */
M2_API void m2_lua_unref(void* L, int ref);

/* Push the value ref holds: lua_rawgeti(L, LUA_REGISTRYINDEX, ref). A released ref's slot holds the
 * registry's free-list link (a number), not nil. Nothing is pushed for an unusable L. */
M2_API void m2_lua_push_ref(void* L, int ref);

/* lua_pcall(L, nargs, nresults, 0): call the function below the top nargs values with them as its
 * arguments; it and they are popped. Pass nresults 0: the SDK reads no results off the stack, so any
 * results asked for stay there. Returns 0 when the function returned; the Lua status when it raised an
 * error (2 runtime, 4 out of memory, 5 error in the error handler), whose message is logged and popped;
 * -1 when it refused to call (an unusable L, nargs < 0, nresults < -1, fewer than nargs + 1 values in
 * the frame, the game's Lua not resolved), with nothing called and the stack unchanged. Tick callbacks do
 * not run on this thread while the call does, and m2_lua_in_exec() returns 1. */
M2_API int m2_lua_pcall(void* L, int nargs, int nresults);

#endif /* M2_LUA_H */
