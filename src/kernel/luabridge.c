#include "src/kernel/luabridge.h"

#include "cbor.h"
#include "plugins/builtin/embedded.h"

#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_EXACT 9007199254740992LL /* 2^53 */

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

int qwe_lua_register_builtins(lua_State *L)
{
	const struct qwe_embedded *m;

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "preload");
	for (m = qwe_embedded_modules; m->name; m++) {
		if (luaL_loadbuffer(L, (const char *)m->data, m->len, m->name) != 0) {
			fprintf(stderr, "qwe: built-in plugin %s: %s\n", m->name, lua_tostring(L, -1));
			lua_settop(L, 0);
			return -1;
		}
		lua_setfield(L, -2, m->name);
	}
	lua_pop(L, 2);
	return 0;
}
