/* qwe.cbor from both sides: the C API and the Lua module. */
#include "greatest.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luavm.h"

#include <lauxlib.h>
#include <string.h>

/* Runs a Lua chunk that must not raise; on failure reports its message. */
static enum greatest_test_res run_lua(lua_State *L, const char *chunk)
{
	if (luaL_loadstring(L, chunk) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
		GREATEST_FAILm(lua_tostring(L, -1));
	}
	return GREATEST_TEST_RES_PASS;
}

#define LUA(L, chunk)                                                     \
	do {                                                              \
		enum greatest_test_res r_ = run_lua(L, chunk);            \
		if (r_ != GREATEST_TEST_RES_PASS)                         \
			return r_;                                        \
	} while (0)

TEST roundtrip_types(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "local function same(a, b, path)\n"
	    "  path = path or 'value'\n"
	    "  assert(type(a) == type(b), path .. ': type ' .. type(a) .. ' vs ' .. type(b))\n"
	    "  if type(a) ~= 'table' then assert(a == b, path .. ': ' .. tostring(a) .. ' vs ' .. tostring(b)); return end\n"
	    "  assert(getmetatable(a) == getmetatable(b), path .. ': metatable differs')\n"
	    "  for k, v in pairs(a) do same(v, b[k], path .. '.' .. tostring(k)) end\n"
	    "  for k in pairs(b) do assert(a[k] ~= nil, path .. ': extra key ' .. tostring(k)) end\n"
	    "end\n"
	    "local doc = cbor.map({\n"
	    "  text = 'héllo', int = 42, neg = -7, big = 2^53, float = 1.5, yes = true, no = false,\n"
	    "  nothing = cbor.null,\n"
	    "  list = cbor.array({ 1, 'two', cbor.array({}), cbor.map({ k = 'v' }) }),\n"
	    "  secret = cbor.secret('v1:abc'),\n"
	    "})\n"
	    "local bytes = assert(cbor.encode(doc))\n"
	    "local back = assert(cbor.decode(bytes))\n"
	    "same(doc, back)\n"
	    "-- null stays the one sentinel, and a secret stays a distinct type\n"
	    "assert(back.nothing == cbor.null)\n"
	    "assert(cbor.is_secret(back.secret) and back.secret.value == 'v1:abc')\n"
	    "assert(not cbor.is_secret(back.text) and not cbor.is_secret(back.list))\n"
	    "assert(tostring(back.secret) == '***', 'a secret must not print its value')\n"
	    "-- encoding is deterministic\n"
	    "assert(cbor.encode(back) == bytes)\n");
	lua_close(L);
	PASS();
}

TEST empty_array_vs_object(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "local arr = assert(cbor.decode('\\x80'))   -- []\n"
	    "local obj = assert(cbor.decode('\\xa0'))   -- {}\n"
	    "assert(cbor.is_array(arr) and not cbor.is_map(arr))\n"
	    "assert(cbor.is_map(obj) and not cbor.is_array(obj))\n"
	    "assert(getmetatable(arr) ~= getmetatable(obj))\n"
	    "-- and they encode back to what they were\n"
	    "assert(cbor.encode(arr) == '\\x80')\n"
	    "assert(cbor.encode(obj) == '\\xa0')\n");
	lua_close(L);
	PASS();
}

TEST rejects_big_int(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "-- 2^53 is the largest integer a Lua number holds exactly: fine\n"
	    "assert(cbor.decode('\\x1b\\x00\\x20\\x00\\x00\\x00\\x00\\x00\\x00') == 2^53)\n"
	    "-- 2^53 + 1 is not\n"
	    "local v, err = cbor.decode('\\x1b\\x00\\x20\\x00\\x00\\x00\\x00\\x00\\x01')\n"
	    "assert(v == nil and err:find('2^53', 1, true), tostring(err))\n"
	    "local v2 = cbor.decode('\\x3b\\x00\\x20\\x00\\x00\\x00\\x00\\x00\\x01')\n"
	    "assert(v2 == nil, 'negative side too')\n");
	lua_close(L);
	PASS();
}

TEST depth_limit(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "-- n arrays nested in each other, innermost empty: 0x81 x (n-1), 0x80\n"
	    "local function nested_bytes(n) return string.rep('\\x81', n - 1) .. '\\x80' end\n"
	    "assert(cbor.decode(nested_bytes(64)), 'the limit itself is fine')\n"
	    "local v, err = cbor.decode(nested_bytes(65))\n"
	    "assert(v == nil and err:find('too deep', 1, true), tostring(err))\n"
	    "-- the same limit applies when encoding\n"
	    "local function nested_table(n) local t = cbor.array({}); for _ = 2, n do t = cbor.array({ t }) end return t end\n"
	    "assert(cbor.encode(nested_table(64)))\n"
	    "local e, eerr = cbor.encode(nested_table(65))\n"
	    "assert(e == nil and eerr:find('too deep', 1, true), tostring(eerr))\n");
	lua_close(L);
	PASS();
}

SUITE(lua_cbor)
{
	RUN_TEST(roundtrip_types);
	RUN_TEST(empty_array_vs_object);
	RUN_TEST(rejects_big_int);
	RUN_TEST(depth_limit);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(lua_cbor);
	GREATEST_MAIN_END();
}
