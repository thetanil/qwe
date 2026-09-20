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

TEST decode_refuses_malformed_input(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "local function bad(bytes, what, want)\n"
	    "  local v, err = cbor.decode(bytes)\n"
	    "  assert(v == nil and type(err) == 'string', what .. ': decoded')\n"
	    "  if want then assert(err:find(want, 1, true), what .. ': ' .. err) end\n"
	    "end\n"
	    "bad('', 'empty input')\n"
	    "bad('\\x45ab', 'byte string cut short')\n"
	    "bad('\\x65ab', 'text string cut short')\n"
	    "bad('\\xa1\\x01\\x02', 'integer map key', 'map key is not a string')\n"
	    "bad('\\xa1\\x65ab', 'map key cut short')\n"
	    "bad('\\x81\\x65ab', 'array element cut short')\n"
	    "bad('\\x82\\x01', 'array cut short')\n"
	    "bad('\\xa2\\x61k\\x01', 'map cut short')\n"
	    "bad('\\xc1\\x01', 'a tag that is not ours', 'unsupported CBOR tag')\n"
	    "bad('\\xd9\\x80\\x00\\x01', 'a secret around a number', 'a secret must wrap a text string')\n"
	    "bad('\\xd9\\x80\\x00\\x65ab', 'a secret cut short')\n"
	    "bad('\\xf9\\x3c\\x00', 'a half float', 'unsupported CBOR type')\n"
	    "assert(cbor.decode('\\x42ab') == 'ab', 'a byte string decodes to its bytes')\n"
	    "assert(cbor.decode('\\xfa\\x3f\\xc0\\x00\\x00') == 1.5, 'a 32-bit float')\n");
	lua_close(L);
	PASS();
}

TEST encode_refuses_what_it_cannot_hold(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "local function bad(v, what, want)\n"
	    "  local s, err = cbor.encode(v)\n"
	    "  assert(s == nil and type(err) == 'string', what .. ': encoded')\n"
	    "  assert(err:find(want, 1, true), what .. ': ' .. err)\n"
	    "end\n"
	    "bad(print, 'a function', 'cannot encode this Lua type')\n"
	    "bad(io.stdout, 'a userdata', 'cannot encode this Lua type')\n"
	    "bad(cbor.map({ [1] = 'x' }), 'an integer key in a map', 'map key is not a string')\n"
	    "bad(cbor.map({ a = print }), 'a function inside a map', 'cannot encode this Lua type')\n"
	    "bad(cbor.array({ print }), 'a function inside an array', 'cannot encode this Lua type')\n"
	    "local s = cbor.secret('x'); s.value = nil\n"
	    "bad(s, 'a secret with no value', 'a secret needs a string value')\n"
	    "-- tables with no metatable: a sequence is an array, anything else a map\n"
	    "assert(cbor.is_array(cbor.decode(cbor.encode({ 1, 2 }))))\n"
	    "assert(cbor.is_map(cbor.decode(cbor.encode({}))))\n"
	    "assert(cbor.is_map(cbor.decode(cbor.encode({ a = 1 }))))\n"
	    "-- the constructors and predicates on things they do not expect\n"
	    "assert(cbor.is_array(cbor.array()) and cbor.is_map(cbor.map()))\n"
	    "assert(not pcall(cbor.array, 5) and not pcall(cbor.map, 'x'))\n"
	    "assert(cbor.is_array({}) == false and cbor.is_map(5) == false and cbor.is_secret({}) == false)\n");
	lua_close(L);
	PASS();
}

/* Values bigger than the encoder's first 256-byte buffer, at every depth it can
 * run out of room: the buffer grows and the result is the same. */
TEST encode_grows_its_buffer(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local cbor = require('qwe.cbor')\n"
	    "local big = string.rep('x', 1000)\n"
	    "local doc = cbor.map({ s = big, a = cbor.array({ big, 1, big }), m = cbor.map({ k = big }),\n"
	    "                       secret = cbor.secret(big) })\n"
	    "for i = 1, 20 do doc['key' .. i] = i end -- more keys than the sort buffer starts with\n"
	    "local back = assert(cbor.decode(assert(cbor.encode(doc))))\n"
	    "assert(back.s == big and back.a[3] == big and back.m.k == big and back.secret.value == big)\n"
	    "assert(back.key20 == 20)\n"
	    "assert(#assert(cbor.encode(big)) > 1000 and #assert(cbor.encode(cbor.array({ big }))) > 1000)\n");
	lua_close(L);
	PASS();
}

/* A userdata that is not the null sentinel has no CBOR form. */
TEST encode_refuses_a_light_userdata(void)
{
	lua_State *L = qwe_lua_new();
	uint8_t *out = NULL;
	size_t n = 0;
	char err[128];
	int dummy;

	ASSERT(L != NULL);
	lua_pushlightuserdata(L, &dummy);
	ASSERT_EQ(-1, qwe_lua_to_cbor(L, -1, &out, &n, err, sizeof err));
	ASSERT(strstr(err, "cannot encode this userdata") != NULL);
	lua_close(L);
	PASS();
}

SUITE(lua_cbor)
{
	RUN_TEST(roundtrip_types);
	RUN_TEST(empty_array_vs_object);
	RUN_TEST(rejects_big_int);
	RUN_TEST(depth_limit);
	RUN_TEST(decode_refuses_malformed_input);
	RUN_TEST(encode_refuses_what_it_cannot_hold);
	RUN_TEST(encode_grows_its_buffer);
	RUN_TEST(encode_refuses_a_light_userdata);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(lua_cbor);
	GREATEST_MAIN_END();
}
