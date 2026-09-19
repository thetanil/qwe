/* qwe.cbor: CBOR <-> Lua values, as a C API and as a Lua module.
 *
 *   map      -> table with metatable qwe.cbor.map_mt
 *   array    -> table with metatable qwe.cbor.array_mt (so [] and {} differ)
 *   null     -> qwe.cbor.null (lightuserdata NULL)
 *   secret   -> { value = "<text>" } with metatable qwe.cbor.secret_mt
 *   text/byte string -> string; integer/float -> number
 *
 * Integers beyond 2^53 and nesting beyond QWE_LUA_MAX_DEPTH are errors. Byte
 * strings decode to Lua strings and encode back as text strings. */
#ifndef QWE_KERNEL_LUACBOR_H
#define QWE_KERNEL_LUACBOR_H

#include <lua.h>
#include <stddef.h>
#include <stdint.h>

#define QWE_LUA_MAX_DEPTH 64

/* Pushes the CBOR document as a Lua value. Returns 0 with the value on the
 * stack, or -1 with nothing pushed and err filled. */
int qwe_cbor_to_lua(lua_State *L, const uint8_t *buf, size_t len, char *err, size_t err_size);

/* Encodes the Lua value at idx. Returns 0 and a malloc'd buffer, or -1 with
 * err filled. A table without a metatable is an array if it has a sequence
 * part and a map otherwise. Map keys are written in sorted order. */
int qwe_lua_to_cbor(lua_State *L, int idx, uint8_t **out, size_t *len, char *err, size_t err_size);

/* Opens the Lua module; call it through package.preload["qwe.cbor"]. */
int luaopen_qwe_cbor(lua_State *L);

#endif
