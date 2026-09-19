#include "src/kernel/luavm.h"

#include "src/kernel/lua/embedded.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luafs.h"

#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>

int luaopen_lpeg(lua_State *L);

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
	for (m = qwe_embedded_modules; m->name; m++) {
		if (luaL_loadbuffer(L, (const char *)m->data, m->len, m->name) != 0) {
			fprintf(stderr, "qwe: built-in module %s: %s\n", m->name, lua_tostring(L, -1));
			lua_close(L);
			return NULL;
		}
		lua_setfield(L, -2, m->name);
	}
	lua_pop(L, 2);
	return L;
}
