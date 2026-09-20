#include "src/kernel/luasecrets.h"

#include "src/kernel/redact.h"
#include "src/secrets/envelope.h"
#include "src/secrets/keyfile.h"

#include <lauxlib.h>
#include <sodium.h>
#include <stdlib.h>
#include <string.h>

static uint8_t run_key[QWE_KEY_BYTES];
static int key_loaded;

/* The key is read the first time a secret is needed, so a workflow without
 * secrets never needs a key file. */
static int ensure_key(lua_State *L)
{
	char err[512];

	if (key_loaded)
		return 0;
	if (qwe_key_load(NULL, run_key, err, sizeof err) < 0) {
		lua_pushstring(L, err);
		return lua_error(L);
	}
	key_loaded = 1;
	return 0;
}

static int secrets_reveal(lua_State *L)
{
	const char *text, *why = NULL;
	uint8_t *plain;
	size_t n;

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "value");
	text = lua_tostring(L, -1);
	if (!text)
		return luaL_error(L, "not a secret");
	ensure_key(L);
	if (qwe_envelope_open(run_key, text, &plain, &n, &why) < 0) {
		lua_pushfstring(L, "cannot decrypt a secret: %s", why);
		return lua_error(L);
	}
	qwe_redact_add((const char *)plain, n);
	lua_pushlstring(L, (const char *)plain, n);
	sodium_memzero(plain, n + 1);
	free(plain);
	return 1;
}

static int secrets_mask(lua_State *L)
{
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);

	qwe_redact_add(s, n);
	return 0;
}

static int secrets_check(lua_State *L)
{
	const char *why = NULL;

	if (qwe_envelope_check(luaL_checkstring(L, 1), &why) < 0) {
		lua_pushnil(L);
		lua_pushstring(L, why);
		return 2;
	}
	lua_pushboolean(L, 1);
	return 1;
}

int luaopen_qwe_secrets(lua_State *L)
{
	lua_createtable(L, 0, 3);
	lua_pushcfunction(L, secrets_reveal);
	lua_setfield(L, -2, "reveal");
	lua_pushcfunction(L, secrets_mask);
	lua_setfield(L, -2, "mask");
	lua_pushcfunction(L, secrets_check);
	lua_setfield(L, -2, "check");
	return 1;
}
