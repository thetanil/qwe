/* Compiles Lua files to LuaJIT bytecode and writes them as a C table, so they
 * can be linked into the qwe binary. Runs on the build host, at build time.
 *
 * usage: bcembed <out.c> <module>=<file> ...
 *
 * A file ending in .json is not code: it is embedded as a module that returns
 * its text as one string. */
#include "src/kernel/put.h"
#include "src/kernel/errstr.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The largest source bcembed takes. Its biggest input today is luacheck's
 * parser.lua, 31 KB, so this is about 30 times that. */
#define BCEMBED_MAX_SOURCE (1024L * 1024L)

static int writer(lua_State *L, const void *p, size_t sz, void *ud)
{
	FILE *out = ud;
	const unsigned char *b = p;
	size_t i;

	(void)L;
	for (i = 0; i < sz; i++)
		qwe_out_fmt(out, "%u,%s", b[i], (i % 24 == 23) ? "\n" : "");
	return 0;
}

static char *slurp(const char *path, size_t *len)
{
	FILE *fp = fopen(path, "rb");
	char *buf;
	long n;

	if (!fp)
		return NULL;
	/* fp is read-only: a failed fclose loses nothing */
	n = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
	if (n < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		(void)fclose(fp);
		return NULL;
	}
	if (n > BCEMBED_MAX_SOURCE) {
		(void)fclose(fp);
		errno = EFBIG;
		return NULL;
	}
	buf = malloc((size_t)n + 1);
	if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	(void)fclose(fp);
	if (buf)
		buf[n] = '\0';
	*len = (size_t)n;
	return buf;
}

int main(int argc, char **argv)
{
	lua_State *L = luaL_newstate();
	FILE *out;
	int i, bad;

	if (argc < 2 || !(out = fopen(argv[1], "w"))) {
		qwe_diag("usage: bcembed <out.c> <module>=<file> ...\n");
		return 2;
	}
	qwe_out_str(out, "#include \"src/kernel/lua/embedded.h\"\n\n");
	for (i = 2; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		size_t len, n;
		char *src, *name, *chunk;
		int is_json;

		if (!eq) {
			qwe_diag("bcembed: bad argument %s\n", argv[i]);
			(void)fclose(out); /* failing already */
			return 2;
		}
		name = strndup(argv[i], (size_t)(eq - argv[i]));
		src = slurp(eq + 1, &len);
		if (!src) {
			qwe_diag("bcembed: cannot read %s: %s\n", eq + 1, qwe_strerror(errno));
			free(name);
			(void)fclose(out); /* failing already */
			return 1;
		}
		is_json = strlen(eq + 1) > 5 && !strcmp(eq + 1 + strlen(eq + 1) - 5, ".json");
		if (is_json) {
			/* return [=====[ ... ]=====]: 22 bytes around the text, and a NUL */
			int w;

			chunk = malloc(len + 32);
			w = chunk ? snprintf(chunk, len + 32, "return [=====[\n%s]=====]", src) : -1;
			if (w < 0 || (size_t)w >= len + 32) {
				qwe_diag("bcembed: cannot wrap %s\n", eq + 1);
				free(chunk);
				free(src);
				free(name);
				(void)fclose(out); /* failing already */
				return 1;
			}
			n = (size_t)w;
		} else {
			chunk = src;
			n = len;
		}
		{
			char chunkname[256];
			int w = snprintf(chunkname, sizeof chunkname, "=%s", name);

			/* the chunk name is what a Lua error names the module by */
			if (w < 0 || (size_t)w >= sizeof chunkname) {
				qwe_diag("bcembed: module name too long: %.64s...\n", name);
				if (is_json)
					free(src);
				free(chunk);
				free(name);
				(void)fclose(out); /* failing already */
				return 1;
			}
			if (luaL_loadbuffer(L, chunk, n, chunkname) != 0) {
				qwe_diag("bcembed: %s\n", lua_tostring(L, -1));
				if (is_json)
					free(src);
				free(chunk);
				free(name);
				(void)fclose(out); /* failing already */
				return 1;
			}
		}
		qwe_out_fmt(out, "static const unsigned char data%d[] = {\n", i);
		lua_dump(L, writer, out);
		qwe_out_str(out, "0};\n");
		lua_pop(L, 1);
		if (is_json)
			free(src);
		free(chunk);
		free(name);
	}
	qwe_out_str(out, "\nconst struct qwe_embedded qwe_embedded_modules[] = {\n");
	for (i = 2; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		qwe_out_fmt(out, "\t{\"%.*s\", \"%s\", data%d, sizeof data%d - 1},\n", (int)(eq - argv[i]), argv[i], eq + 1, i, i);
	}
	qwe_out_str(out, "\t{0, 0, 0, 0},\n};\n");
	/* A truncated table can still compile: a write that failed must fail the build. */
	bad = ferror(out);
	if (fclose(out) != 0 || bad) {
		qwe_diag("bcembed: cannot write %s\n", argv[1]);
		return 1;
	}
	return 0;
}
