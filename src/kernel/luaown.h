/* Handing a malloc'd buffer to a Lua API call that can raise.
 *
 * The rule (ticket sca-round3/02): no C resource is live across a Lua API
 * call that can raise (longjmp) -- a malloc'd buffer included. Most of the
 * time that means validating every argument (luaL_check*) before the first
 * allocation, so the raise happens with nothing yet to free. But a function
 * that hands a C buffer *to* Lua -- lua_pushlstring and friends, which copy
 * from it and can themselves raise on a Lua allocation failure mid-copy --
 * has no such ordering: the buffer must still exist at the one call that
 * might raise while holding it.
 *
 * qwe_lua_own closes that gap by transferring the buffer to a Lua userdata
 * with a __gc that frees it, before the risky call. From that point the
 * buffer is reachable from the Lua stack: if the next call raises, the
 * userdata survives the longjmp (Lua only unwinds its own stack, not the
 * objects reachable from it) and its __gc frees the buffer once collected,
 * instead of it being lost. */
#ifndef QWE_KERNEL_LUAOWN_H
#define QWE_KERNEL_LUAOWN_H

#include <lua.h>
#include <stddef.h>

/* Wraps *p (a malloc'd buffer, or NULL) in a new userdata pushed on top of
 * the stack, and clears *p: it is Lua's to free from here on. Returns the
 * wrapper's absolute stack index. Takes char ** (not void **) so a caller
 * never needs a multi-level pointer cast to use it (ticket sca-round3/10). */
int qwe_lua_own(lua_State *L, char **p);

/* The second half of qwe_lua_own_pushlstring, split out so a caller can do
 * something -- a test's forced raise, say -- between the transfer and the
 * push, while the buffer is already safely owned: pushes the len-byte
 * string held by the buffer qwe_lua_own wrapped at stack index idx, and
 * drops the wrapper. */
void qwe_lua_own_finish(lua_State *L, int idx, size_t len);

/* qwe_lua_own(p) then qwe_lua_own_finish, leaving exactly the string on top
 * of the stack -- the same net stack effect as lua_pushlstring(L, *p, len),
 * except that a raise during the copy frees *p instead of leaking it. *p may
 * be NULL only if len is 0. */
void qwe_lua_own_pushlstring(lua_State *L, char **p, size_t len);

/* Same as qwe_lua_own, for a block whose pointee type is not char -- an
 * array of pointers borrowed from Lua, say, freed as a single block, not
 * per element (qwe_lua_own_strv below is for the "per element too" shape).
 * Takes void ** (unlike qwe_lua_own): the cast this needs at the call site
 * is explicit, so it is not what ticket sca-round3/10 means by an implicit
 * multi-level pointer conversion, and belongs at the one call site that
 * knows the real pointee type, not hidden in here. */
int qwe_lua_own_block(lua_State *L, void **p);

/* Takes back the pointer wrapped (by qwe_lua_own or qwe_lua_own_block) at
 * stack index idx: returns it and disarms the wrapper's __gc, for a caller
 * that reaches the end of the risky section itself and wants to free the
 * block right away rather than wait for it to be collected. */
void *qwe_lua_own_take(lua_State *L, int idx);

/* Wraps an array of n malloc'd strings, *v[0..n-1], plus the array *v
 * itself (also malloc'd) in a new userdata whose __gc frees all of it --
 * for building a Lua value (a table, say) element by element from such an
 * array, where any of the pushes can raise while later elements are still
 * unowned C memory. Clears *v. Returns the wrapper's stack index. */
int qwe_lua_own_strv(lua_State *L, char ***v, size_t n);

/* Frees the array wrapped (by qwe_lua_own_strv) at stack index idx right
 * away, and disarms its __gc, instead of waiting for it to be collected. */
void qwe_lua_own_strv_release(lua_State *L, int idx);

#endif
