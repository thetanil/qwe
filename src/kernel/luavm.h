/* The Lua state every part of qwe uses: LuaJIT with the built-in modules
 * (compiled-in bytecode) available to require(). */
#ifndef QWE_KERNEL_LUAVM_H
#define QWE_KERNEL_LUAVM_H

#include <lua.h>

/* Returns a new state, or NULL if a built-in module fails to load. Modules
 * are registered in package.preload, plus the C modules lpeg, qwe.cbor, qwe.exec and
 * qwe.fs. package.path and cpath are empty: nothing loads from disk. */
lua_State *qwe_lua_new(void);

/* Writes the Lua coverage gathered so far (a no-op unless enabled). A forked step
 * child calls it before it execs: the exec discards the child's counts. */
void qwe_lua_coverage_flush(lua_State *L);

#endif
