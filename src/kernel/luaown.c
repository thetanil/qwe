#include "src/kernel/luaown.h"

#include <lauxlib.h>
#include <stdlib.h>

struct qwe_owned {
	void *p;
};

static int owned_gc(lua_State *L)
{
	struct qwe_owned *o = lua_touserdata(L, 1);

	free(o->p);
	o->p = NULL;
	return 0;
}

static int own_block(lua_State *L, void **p)
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

int qwe_lua_own(lua_State *L, char **p)
{
	return own_block(L, (void **)p);
}

int qwe_lua_own_block(lua_State *L, void **p)
{
	return own_block(L, p);
}

void *qwe_lua_own_take(lua_State *L, int idx)
{
	struct qwe_owned *o = lua_touserdata(L, idx);
	void *p = o->p;

	o->p = NULL;
	return p;
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

struct qwe_owned_strv {
	char **v;
	size_t n;
};

static void free_strv(struct qwe_owned_strv *o)
{
	size_t i;

	if (!o->v)
		return;
	for (i = 0; i < o->n; i++)
		free(o->v[i]);
	free(o->v);
	o->v = NULL;
}

static int owned_strv_gc(lua_State *L)
{
	free_strv(lua_touserdata(L, 1));
	return 0;
}

int qwe_lua_own_strv(lua_State *L, char ***v, size_t n)
{
	struct qwe_owned_strv *o = lua_newuserdata(L, sizeof *o);

	o->v = *v;
	o->n = n;
	*v = NULL;
	if (luaL_newmetatable(L, "qwe.owned_strv")) {
		lua_pushcfunction(L, owned_strv_gc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	return lua_gettop(L);
}

void qwe_lua_own_strv_release(lua_State *L, int idx)
{
	free_strv(lua_touserdata(L, idx));
}
