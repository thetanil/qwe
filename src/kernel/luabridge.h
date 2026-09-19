/* CBOR <-> Lua tables, and the built-in plugin modules. */
#ifndef QWE_KERNEL_LUABRIDGE_H
#define QWE_KERNEL_LUABRIDGE_H

#include <lua.h>
#include <stddef.h>
#include <stdint.h>

#define QWE_LUA_MAX_DEPTH 64

/* Pushes the CBOR document as a Lua value (maps and arrays become tables,
 * null becomes lightuserdata NULL). Returns 0 with the value on the stack, or
 * -1 with nothing pushed and err filled. Integers beyond 2^53 are rejected,
 * because Lua numbers are doubles. */
int qwe_cbor_to_lua(lua_State *L, const uint8_t *buf, size_t len, char *err, size_t err_size);

/* Makes every built-in plugin module available to require(). */
int qwe_lua_register_builtins(lua_State *L);

#endif
