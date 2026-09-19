/* The Lua state every part of qwe uses: LuaJIT with the built-in modules
 * (compiled-in bytecode) available to require(). */
#ifndef QWE_KERNEL_LUAVM_H
#define QWE_KERNEL_LUAVM_H

#include <lua.h>

/* Returns a new state, or NULL if a built-in module fails to load. Modules
 * are registered in package.preload, plus the C modules lpeg, qwe.cbor and
 * qwe.fs. package.path and cpath are empty: nothing loads from disk. */
lua_State *qwe_lua_new(void);

#endif
