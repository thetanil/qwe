#include "src/kernel/luavm.h"

#include "src/kernel/lua/embedded.h"
#include "src/kernel/luacbor.h"

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
	lua_getfield(L, -1, "preload");
	lua_pushcfunction(L, luaopen_lpeg);
	lua_setfield(L, -2, "lpeg");
	lua_pushcfunction(L, luaopen_qwe_cbor);
	lua_setfield(L, -2, "qwe.cbor");
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
