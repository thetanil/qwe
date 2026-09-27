#include "src/kernel/luaown.h"

#include <lauxlib.h>
#include <stdlib.h>

struct qwe_owned {
	char *p;
};

static int owned_gc(lua_State *L)
{
	struct qwe_owned *o = lua_touserdata(L, 1);

	free(o->p);
	o->p = NULL;
	return 0;
}

int qwe_lua_own(lua_State *L, char **p)
{
	struct qwe_owned *o = lua_newuserdata(L, sizeof *o);

	o->p = *p;
	*p = NULL;
	if (luaL_newmetatable(L, "qwe.owned")) {
		lua_pushcfunction(L, owned_gc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	return lua_gettop(L);
}

void qwe_lua_own_finish(lua_State *L, int idx, size_t len)
{
	struct qwe_owned *o = lua_touserdata(L, idx);

	lua_pushlstring(L, o->p ? o->p : "", len);
	lua_replace(L, idx);
}

void qwe_lua_own_pushlstring(lua_State *L, char **p, size_t len)
{
	int idx = qwe_lua_own(L, p);

	qwe_lua_own_finish(L, idx, len);
}
