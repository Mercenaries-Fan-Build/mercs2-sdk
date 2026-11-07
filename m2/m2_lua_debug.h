/* m2_lua_debug.h — the Lua debugger in the game, for the transport that carries its protocol.
 *
 * m2-sdk.dll is the retail host of the Workshop Missions debugger's protocol (JSON frames whose `type`
 * starts with `debug.`; mercs2_formats::lua_debug). A transport (the lua-bridge Shipment) hands it each
 * debug frame a client sends and registers the one sink every answer and event goes out through:
 *
 *     static int Send(unsigned conn, const char* json, int len, void* ud) { ...; return delivered; }
 *     static void WhilePaused(void* L, void* ud) { ... run queued chunks with m2_lua_exec(L, ...) ... }
 *
 *     m2_lua_debug_set_sink(Send, ctx);
 *     m2_lua_debug_on_paused(WhilePaused, ctx);
 *     // per frame a client sends:  if (!m2_lua_debug_request(conn, json, len)) reject it with m2_lua_last_error()
 *     // when a client's connection closes:  m2_lua_debug_disconnected(conn);
 *
 * Requests are handled on the game thread. A breakpoint or step blocks the game thread inside the Lua hook
 * until a client continues or the player dismisses the notice m2-sdk.dll shows (OK, or F8); while it is
 * blocked the paused callbacks run on the game thread, so chunks still run (no debug events are raised by
 * them). Nothing is hooked until a client attaches; detaching or disconnecting removes the hook.
 *
 * Calling conventions: every function and callback is cdecl. Register the sink and the paused callbacks
 * before m2_lua_install(): requests are handled on the game thread from the first tick after it, and an
 * answer due before a sink is registered gives the debugger up.
 */
#ifndef M2_LUA_DEBUG_H
#define M2_LUA_DEBUG_H

#include "m2_api.h"

/* Deliver one frame (`len` bytes of JSON, not NUL-terminated) to client `conn`. Return 1 when it is
 * queued for the client, 0 when it cannot be (gone, or its outbox is full): the debugger then gives the
 * client up and removes the hook, rather than leave a gap in the events. Called on the game thread with
 * the debugger's lock held: only queue the frame; never call an m2_lua_debug_* function from inside. */
typedef int (*m2_lua_debug_sink)(unsigned conn, const char* json, int len, void* ud);

/* Called on the game thread, over and over, while the debugger has it paused. */
typedef void (*m2_lua_debug_paused_cb)(void* L, void* ud);

/* Register the sink. One per process: 0 when one is registered already or cb is NULL. */
M2_API int m2_lua_debug_set_sink(m2_lua_debug_sink cb, void* ud);

/* Add a paused callback. 0 when cb is NULL. */
M2_API int m2_lua_debug_on_paused(m2_lua_debug_paused_cb cb, void* ud);

/* 1 while the debugger has the game thread paused. */
M2_API int m2_lua_debug_paused(void);

/* Queue a debug request from client `conn` (any number the transport uses to tell its clients apart).
 * 1 when queued; 0 when the frame is not a valid debug request (a REPL request has no `type`), with the
 * reason in m2_lua_last_error(). Callable from any thread. */
M2_API int m2_lua_debug_request(unsigned conn, const char* json, int len);

/* Client `conn` is gone: if it held the debugger, the hook comes off and a paused game resumes. */
M2_API void m2_lua_debug_disconnected(unsigned conn);

#endif /* M2_LUA_DEBUG_H */
