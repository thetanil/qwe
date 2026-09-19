/* Runs a Lua script in qwe's own Lua state (built-in modules available), for
 * tests: luarun <script.lua> [args...]. The arguments are the script's `...`. */
#include "src/kernel/luavm.h"

#include <lauxlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	lua_State *L;
	int i, rc;

	if (argc < 2) {
		fprintf(stderr, "usage: luarun <script.lua> [args...]\n");
		return 2;
	}
	L = qwe_lua_new();
	if (!L)
		return 2;
	if (luaL_loadfile(L, argv[1]) != 0) {
		fprintf(stderr, "luarun: %s\n", lua_tostring(L, -1));
		return 2;
	}
	for (i = 2; i < argc; i++)
		lua_pushstring(L, argv[i]);
	rc = lua_pcall(L, argc - 2, 0, 0);
	if (rc != 0) {
		fprintf(stderr, "luarun: %s\n", lua_tostring(L, -1));
		return 1;
	}
	lua_close(L);
	return 0;
}
