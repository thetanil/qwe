#include "src/kernel/luacbor.h"

#include "cbor.h"
#include "src/edge/yaml/secret_tag.h"

#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EXACT 9007199254740992LL /* 2^53 */

#define ARRAY_MT "qwe.cbor.array"
#define MAP_MT "qwe.cbor.map"
#define SECRET_MT "qwe.cbor.secret"

static int secret_tostring(lua_State *L)
{
	lua_pushliteral(L, "***");
	return 1;
}

/* Creates the three metatables in the registry if they are not there yet. */
static void ensure_metatables(lua_State *L)
{
	if (luaL_newmetatable(L, ARRAY_MT)) {
		lua_pushliteral(L, "array");
		lua_setfield(L, -2, "__name");
	}
	lua_pop(L, 1);
	if (luaL_newmetatable(L, MAP_MT)) {
		lua_pushliteral(L, "map");
		lua_setfield(L, -2, "__name");
	}
	lua_pop(L, 1);
	if (luaL_newmetatable(L, SECRET_MT)) {
		lua_pushcfunction(L, secret_tostring);
		lua_setfield(L, -2, "__tostring");
		lua_pushliteral(L, "secret");
		lua_setfield(L, -2, "__metatable"); /* locked */
	}
	lua_pop(L, 1);
}

/* ---- CBOR -> Lua ---- */

static const char *convert(lua_State *L, CborValue *it, int depth);

static const char *convert_string(lua_State *L, CborValue *it, int bytes)
{
	size_t n;
	CborError e;

	if (bytes) {
		uint8_t *s;
		if ((e = cbor_value_dup_byte_string(it, &s, &n, it)) != CborNoError)
			return cbor_error_string(e);
		lua_pushlstring(L, (const char *)s, n);
		free(s);
	} else {
		char *s;
		if ((e = cbor_value_dup_text_string(it, &s, &n, it)) != CborNoError)
			return cbor_error_string(e);
		lua_pushlstring(L, s, n);
		free(s);
	}
	return NULL;
}

static const char *convert_container(lua_State *L, CborValue *it, int depth, int is_map)
{
	CborValue inner;
	const char *err;
	int index = 1;

	if (depth >= QWE_LUA_MAX_DEPTH)
		return "nesting too deep";
	if (cbor_value_enter_container(it, &inner) != CborNoError)
		return "malformed container";
	lua_newtable(L);
	luaL_getmetatable(L, is_map ? MAP_MT : ARRAY_MT);
	lua_setmetatable(L, -2);
	while (!cbor_value_at_end(&inner)) {
		if (is_map) {
			if (!cbor_value_is_text_string(&inner))
				return "map key is not a string";
			if ((err = convert_string(L, &inner, 0)) != NULL)
				return err;
		}
		if ((err = convert(L, &inner, depth + 1)) != NULL)
			return err;
		if (is_map)
			lua_settable(L, -3);
		else
			lua_rawseti(L, -2, index++);
	}
	if (cbor_value_leave_container(it, &inner) != CborNoError)
		return "malformed container";
	return NULL;
}

static const char *convert_tagged(lua_State *L, CborValue *it)
{
	CborTag tag;
	const char *err;

	cbor_value_get_tag(it, &tag);
	if (tag != QWE_SECRET_TAG)
		return "unsupported CBOR tag";
	if (cbor_value_skip_tag(it) != CborNoError || !cbor_value_is_text_string(it))
		return "a secret must wrap a text string";
	lua_newtable(L);
	if ((err = convert_string(L, it, 0)) != NULL)
		return err;
	lua_setfield(L, -2, "value");
	luaL_getmetatable(L, SECRET_MT);
	lua_setmetatable(L, -2);
	return NULL;
}

static const char *convert(lua_State *L, CborValue *it, int depth)
{
	switch (cbor_value_get_type(it)) {
	case CborIntegerType: {
		int64_t v;
		if (cbor_value_get_int64_checked(it, &v) != CborNoError || v > MAX_EXACT || v < -MAX_EXACT)
			return "integer out of range (beyond 2^53)";
		lua_pushnumber(L, (lua_Number)v);
		return cbor_value_advance_fixed(it) == CborNoError ? NULL : "malformed integer";
	}
	case CborTextStringType:
		return convert_string(L, it, 0);
	case CborByteStringType:
		return convert_string(L, it, 1);
	case CborMapType:
		return convert_container(L, it, depth, 1);
	case CborArrayType:
		return convert_container(L, it, depth, 0);
	case CborTagType:
		return convert_tagged(L, it);
	case CborBooleanType: {
		bool b;
		cbor_value_get_boolean(it, &b);
		lua_pushboolean(L, b);
		return cbor_value_advance_fixed(it) == CborNoError ? NULL : "malformed boolean";
	}
	case CborNullType:
	case CborUndefinedType:
		lua_pushlightuserdata(L, NULL);
		return cbor_value_advance_fixed(it) == CborNoError ? NULL : "malformed null";
	case CborDoubleType: {
		double d;
		cbor_value_get_double(it, &d);
		lua_pushnumber(L, d);
		return cbor_value_advance_fixed(it) == CborNoError ? NULL : "malformed float";
	}
	case CborFloatType: {
		float f;
		cbor_value_get_float(it, &f);
		lua_pushnumber(L, f);
		return cbor_value_advance_fixed(it) == CborNoError ? NULL : "malformed float";
	}
	default:
		return "unsupported CBOR type";
	}
}

int qwe_cbor_to_lua(lua_State *L, const uint8_t *buf, size_t len, char *err, size_t err_size)
{
	CborParser parser;
	CborValue it;
	int top = lua_gettop(L);
	const char *msg;

	ensure_metatables(L);
	if (cbor_parser_init(buf, len, 0, &parser, &it) != CborNoError) {
		snprintf(err, err_size, "invalid CBOR");
		return -1;
	}
	msg = convert(L, &it, 0);
	if (msg) {
		lua_settop(L, top);
		snprintf(err, err_size, "%s", msg);
		return -1;
	}
	return 0;
}

/* ---- Lua -> CBOR ---- */

enum kind { K_ARRAY, K_MAP, K_SECRET };

/* The table at idx, classified by its metatable. */
static enum kind classify(lua_State *L, int idx)
{
	enum kind k;

	if (!lua_getmetatable(L, idx)) {
		/* No metatable: a sequence is an array, anything else a map. */
		return lua_objlen(L, idx) > 0 ? K_ARRAY : K_MAP;
	}
	luaL_getmetatable(L, ARRAY_MT);
	if (lua_rawequal(L, -1, -2)) {
		k = K_ARRAY;
	} else {
		lua_pop(L, 1);
		luaL_getmetatable(L, SECRET_MT);
		k = lua_rawequal(L, -1, -2) ? K_SECRET : K_MAP;
	}
	lua_pop(L, 2);
	return k;
}

static int key_cmp(const void *a, const void *b)
{
	const char *const *x = a, *const *y = b;
	return strcmp(*x, *y);
}

static int encode_value(lua_State *L, int idx, CborEncoder *enc, int depth, const char **err);

static int encode_table(lua_State *L, int idx, CborEncoder *enc, int depth, const char **err)
{
	CborEncoder inner;
	enum kind k = classify(L, idx);

	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;
	int rc = CborNoError;

	if (depth >= QWE_LUA_MAX_DEPTH) {
		*err = "nesting too deep";
		return -1;
	}
	if (k == K_SECRET) {
		size_t n = 0;
		const char *s;
		lua_getfield(L, idx, "value");
		s = lua_isstring(L, -1) ? lua_tolstring(L, -1, &n) : NULL;
		if (!s) {
			*err = "a secret needs a string value";
			return -1;
		}
		rc = cbor_encode_tag(enc, QWE_SECRET_TAG);
		if (rc == CborNoError || rc == CborErrorOutOfMemory)
			rc = cbor_encode_text_string(enc, s, n);
		lua_pop(L, 1);
		return rc == CborNoError ? 0 : rc;
	}
	if (k == K_ARRAY) {
		size_t i, n = lua_objlen(L, idx);
		rc = cbor_encoder_create_array(enc, &inner, n);
		for (i = 1; i <= n && (rc == CborNoError || rc == CborErrorOutOfMemory); i++) {
			int r;
			lua_rawgeti(L, idx, (int)i);
			r = encode_value(L, lua_gettop(L), &inner, depth + 1, err);
			lua_pop(L, 1);
			if (r != 0 && r != CborErrorOutOfMemory)
				return r;
			if (r == CborErrorOutOfMemory)
				rc = r;
		}
	} else {
		size_t i, n = 0, cap = 8;
		const char **keys = malloc(cap * sizeof *keys);
		int r2 = 0;

		if (!keys) {
			*err = "out of memory";
			return -1;
		}

		lua_pushnil(L);
		while (lua_next(L, idx)) {
			lua_pop(L, 1); /* the value; the key stays for lua_next */
			if (lua_type(L, -1) != LUA_TSTRING) {
				lua_pop(L, 1);
				free(keys);
				*err = "map key is not a string";
				return -1;
			}
			if (n == cap) {
				const char **grown = realloc(keys, (cap * 2) * sizeof *keys);

				if (!grown) {
					lua_pop(L, 1);
					free(keys);
					*err = "out of memory";
					return -1;
				}
				keys = grown;
				cap *= 2;
			}
			keys[n++] = lua_tostring(L, -1);
		}
		qsort(keys, n, sizeof *keys, key_cmp);
		rc = cbor_encoder_create_map(enc, &inner, n);
		for (i = 0; i < n && r2 == 0; i++) {
			int r;
			lua_pushstring(L, keys[i]);
			rc = cbor_encode_text_stringz(&inner, keys[i]) == CborErrorOutOfMemory ? CborErrorOutOfMemory : rc;
			lua_gettable(L, idx);
			r = encode_value(L, lua_gettop(L), &inner, depth + 1, err);
			lua_pop(L, 1);
			if (r != 0 && r != CborErrorOutOfMemory)
				r2 = r;
			if (r == CborErrorOutOfMemory)
				rc = r;
		}
		free(keys);
		if (r2)
			return r2;
	}
	{
		CborError e = cbor_encoder_close_container(enc, &inner);
		if (e == CborErrorOutOfMemory || rc == CborErrorOutOfMemory)
			return CborErrorOutOfMemory;
		return e == CborNoError ? 0 : -1;
	}
}

/* Returns 0, CborErrorOutOfMemory (buffer too small; caller retries), or -1
 * with *err set. */
static int encode_value(lua_State *L, int idx, CborEncoder *enc, int depth, const char **err)
{
	CborError e;

	switch (lua_type(L, idx)) {
	case LUA_TBOOLEAN:
		e = cbor_encode_boolean(enc, lua_toboolean(L, idx));
		break;
	case LUA_TNUMBER: {
		double d = lua_tonumber(L, idx);
		if (d == (double)(long long)d && d <= (double)MAX_EXACT && d >= -(double)MAX_EXACT)
			e = cbor_encode_int(enc, (int64_t)d);
		else
			e = cbor_encode_double(enc, d);
		break;
	}
	case LUA_TSTRING: {
		size_t n;
		const char *s = lua_tolstring(L, idx, &n);
		e = cbor_encode_text_string(enc, s, n);
		break;
	}
	case LUA_TLIGHTUSERDATA:
		if (lua_touserdata(L, idx) != NULL) {
			*err = "cannot encode this userdata";
			return -1;
		}
		e = cbor_encode_null(enc);
		break;
	case LUA_TTABLE:
		return encode_table(L, idx, enc, depth, err);
	default:
		*err = "cannot encode this Lua type";
		return -1;
	}
	if (e == CborNoError)
		return 0;
	return e == CborErrorOutOfMemory ? CborErrorOutOfMemory : -1;
}

int qwe_lua_to_cbor(lua_State *L, int idx, uint8_t **out, size_t *len, char *err, size_t err_size)
{
	size_t cap = 256;
	int abs = idx < 0 ? lua_gettop(L) + idx + 1 : idx;

	ensure_metatables(L);
	for (;;) {
		uint8_t *buf = malloc(cap);
		CborEncoder enc;
		const char *msg = "cannot encode value";
		int r;

		if (!buf) {
			snprintf(err, err_size, "out of memory");
			return -1;
		}
		cbor_encoder_init(&enc, buf, cap, 0);
		r = encode_value(L, abs, &enc, 0, &msg);
		if (r == 0) {
			*len = cbor_encoder_get_buffer_size(&enc, buf);
			*out = buf;
			return 0;
		}
		free(buf);
		if (r != CborErrorOutOfMemory) {
			snprintf(err, err_size, "%s", msg);
			return -1;
		}
		cap *= 2;
	}
}

/* ---- the Lua module ---- */

static int l_decode(lua_State *L)
{
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);
	char err[128];

	if (qwe_cbor_to_lua(L, (const uint8_t *)s, n, err, sizeof err) < 0) {
		lua_pushnil(L);
		lua_pushstring(L, err);
		return 2;
	}
	return 1;
}

static int l_encode(lua_State *L)
{
	uint8_t *buf;
	size_t n;
	char err[128];

	luaL_checkany(L, 1);
	if (qwe_lua_to_cbor(L, 1, &buf, &n, err, sizeof err) < 0) {
		lua_pushnil(L);
		lua_pushstring(L, err);
		return 2;
	}
	lua_pushlstring(L, (const char *)buf, n);
	free(buf);
	return 1;
}

static int set_mt(lua_State *L, const char *name)
{
	if (lua_isnone(L, 1))
		lua_newtable(L);
	else
		luaL_checktype(L, 1, LUA_TTABLE);
	lua_settop(L, 1);
	luaL_getmetatable(L, name);
	lua_setmetatable(L, 1);
	return 1;
}

static int l_array(lua_State *L) { return set_mt(L, ARRAY_MT); }
static int l_map(lua_State *L) { return set_mt(L, MAP_MT); }

static int has_mt(lua_State *L, const char *name)
{
	int r;

	if (!lua_getmetatable(L, 1))
		return 0;
	luaL_getmetatable(L, name);
	r = lua_rawequal(L, -1, -2);
	lua_pop(L, 2);
	return r;
}

static int l_is_array(lua_State *L) { lua_pushboolean(L, has_mt(L, ARRAY_MT)); return 1; }
static int l_is_map(lua_State *L) { lua_pushboolean(L, has_mt(L, MAP_MT)); return 1; }
static int l_is_secret(lua_State *L) { lua_pushboolean(L, has_mt(L, SECRET_MT)); return 1; }

static int l_secret(lua_State *L)
{
	luaL_checkstring(L, 1);
	lua_settop(L, 1);
	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "value");
	luaL_getmetatable(L, SECRET_MT);
	lua_setmetatable(L, -2);
	return 1;
}

int luaopen_qwe_cbor(lua_State *L)
{
	static const luaL_Reg funcs[] = {
		{"decode", l_decode},   {"encode", l_encode},
		{"array", l_array},     {"map", l_map},
		{"is_array", l_is_array}, {"is_map", l_is_map},
		{"is_secret", l_is_secret}, {"secret", l_secret},
		{NULL, NULL},
	};

	ensure_metatables(L);
	lua_newtable(L);
	luaL_register(L, NULL, funcs);
	luaL_getmetatable(L, ARRAY_MT);
	lua_setfield(L, -2, "array_mt");
	luaL_getmetatable(L, MAP_MT);
	lua_setfield(L, -2, "map_mt");
	luaL_getmetatable(L, SECRET_MT);
	lua_setfield(L, -2, "secret_mt");
	lua_pushlightuserdata(L, NULL);
	lua_setfield(L, -2, "null");
	return 1;
}
