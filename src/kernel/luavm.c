#include "src/kernel/luavm.h"

#include "src/kernel/lua/embedded.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luaexec.h"
#include "src/kernel/luafs.h"
#include "src/kernel/luasecrets.h"

#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int luaopen_lpeg(lua_State *L);

/* Turns on Lua line coverage when QWE_LUA_COVERAGE and COVERAGE_DIR are set (bazel coverage sets the latter). */
static void coverage_enable(lua_State *L)
{
	const struct qwe_embedded *m;
	const char *dir = getenv("COVERAGE_DIR");
	int i = 0;

	if (!getenv("QWE_LUA_COVERAGE") || !dir)
		return;
	lua_getglobal(L, "require");
	lua_pushliteral(L, "qwe.luacov");
	if (lua_pcall(L, 1, 1, 0) != 0) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, "enable");
	lua_pushstring(L, dir);
	lua_newtable(L);
	for (m = qwe_embedded_modules; m->name; m++) {
		if (strncmp(m->path, "third_party/", 12) == 0)
			continue;
		lua_newtable(L);
		lua_pushstring(L, m->name);
		lua_setfield(L, -2, "name");
		lua_pushstring(L, m->path);
		lua_setfield(L, -2, "path");
		lua_rawseti(L, -2, ++i);
	}
	if (lua_pcall(L, 2, 0, 0) != 0)
		lua_pop(L, 1);
	lua_pop(L, 1);
}

void qwe_lua_coverage_flush(lua_State *L)
{
	if (!getenv("QWE_LUA_COVERAGE"))
		return;
	lua_getglobal(L, "require");
	lua_pushliteral(L, "qwe.luacov");
	if (lua_pcall(L, 1, 1, 0) != 0) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, "flush");
	if (lua_pcall(L, 0, 0, 0) != 0)
		lua_pop(L, 1);
	lua_pop(L, 1);
}

lua_State *qwe_lua_new(void)
{
	lua_State *L = luaL_newstate();
	const struct qwe_embedded *m;

	if (!L)
		return NULL;
	luaL_openlibs(L);
	lua_getglobal(L, "package");
	/* Nothing loads from disk: require() finds only the built-in modules, so
	 * a stray .lua file (or LUA_PATH) can never stand in for one. Project
	 * plugins are read by path (qwe.plugins), not through require(). */
	lua_pushliteral(L, "");
	lua_setfield(L, -2, "path");
	lua_pushliteral(L, "");
	lua_setfield(L, -2, "cpath");
	lua_getfield(L, -1, "preload");
	lua_pushcfunction(L, luaopen_lpeg);
	lua_setfield(L, -2, "lpeg");
	lua_pushcfunction(L, luaopen_qwe_cbor);
	lua_setfield(L, -2, "qwe.cbor");
	lua_pushcfunction(L, luaopen_qwe_fs);
	lua_setfield(L, -2, "qwe.fs");
	lua_pushcfunction(L, luaopen_qwe_exec);
	lua_setfield(L, -2, "qwe.exec");
	lua_pushcfunction(L, luaopen_qwe_secrets);
	lua_setfield(L, -2, "qwe.secrets");
	for (m = qwe_embedded_modules; m->name; m++) {
		if (luaL_loadbuffer(L, (const char *)m->data, m->len, m->name) != 0) {
			fprintf(stderr, "qwe: built-in module %s: %s\n", m->name, lua_tostring(L, -1));
			lua_close(L);
			return NULL;
		}
		lua_setfield(L, -2, m->name);
	}
	lua_pop(L, 2);
	coverage_enable(L);
	return L;
}
