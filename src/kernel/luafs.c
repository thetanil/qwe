#define _POSIX_C_SOURCE 200809L
#include "src/kernel/luafs.h"

#include "src/secrets/keyfile.h"

#include <dirent.h>
#include <errno.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int cmp_name(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int fs_list(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	DIR *d = opendir(path);
	struct dirent *e;
	char **names = NULL;
	size_t n = 0, cap = 0, i;

	if (!d) {
		lua_pushnil(L);
		lua_pushstring(L, strerror(errno));
		return 2;
	}
	while ((e = readdir(d))) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (n == cap) {
			char **grown = realloc(names, (cap ? cap * 2 : 16) * sizeof *names);

			if (!grown)
				goto nomem;
			names = grown;
			cap = cap ? cap * 2 : 16;
		}
		names[n] = strdup(e->d_name);
		if (!names[n])
			goto nomem;
		n++;
	}
	closedir(d);
	if (n > 1) /* qsort of a null array is undefined, even for zero elements */
		qsort(names, n, sizeof *names, cmp_name);
	lua_createtable(L, (int)n, 0);
	for (i = 0; i < n; i++) {
		lua_pushstring(L, names[i]);
		lua_rawseti(L, -2, (int)i + 1);
		free(names[i]);
	}
	free(names);
	return 1;
nomem:
	closedir(d);
	while (n-- > 0)
		free(names[n]);
	free(names);
	lua_pushnil(L);
	lua_pushstring(L, strerror(ENOMEM));
	return 2;
}

static int fs_isdir(lua_State *L)
{
	struct stat st;

	lua_pushboolean(L, stat(luaL_checkstring(L, 1), &st) == 0 && S_ISDIR(st.st_mode));
	return 1;
}

static int fs_private_dir(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1), *what = luaL_optstring(L, 2, "the directory");
	char err[512];
	struct stat st;

	if (mkdir(path, 0700) < 0 && errno != EEXIST) {
		lua_pushnil(L);
		lua_pushfstring(L, "cannot create %s %s: %s", what, path, strerror(errno));
		lua_pushstring(L, "create");
		return 3;
	}
	/* lstat: a symlink is refused, whatever it points at */
	if (lstat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
		lua_pushnil(L);
		lua_pushfstring(L, "%s %s is not a directory", what, path);
		return 2;
	}
	if (qwe_private_check(&st, what, path, err, sizeof err) < 0) {
		lua_pushnil(L);
		lua_pushstring(L, err);
		return 2;
	}
	lua_pushboolean(L, 1);
	return 1;
}

int luaopen_qwe_fs(lua_State *L)
{
	lua_createtable(L, 0, 3);
	lua_pushcfunction(L, fs_list);
	lua_setfield(L, -2, "list");
	lua_pushcfunction(L, fs_isdir);
	lua_setfield(L, -2, "isdir");
	lua_pushcfunction(L, fs_private_dir);
	lua_setfield(L, -2, "private_dir");
	return 1;
}
