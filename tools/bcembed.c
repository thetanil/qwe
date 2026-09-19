/* Compiles Lua files to LuaJIT bytecode and writes them as a C table, so they
 * can be linked into the qwe binary. Runs on the build host, at build time.
 *
 * usage: bcembed <out.c> <module>=<file> ...
 *
 * A file ending in .json is not code: it is embedded as a module that returns
 * its text as one string. */
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int writer(lua_State *L, const void *p, size_t sz, void *ud)
{
	FILE *out = ud;
	const unsigned char *b = p;
	size_t i;

	(void)L;
	for (i = 0; i < sz; i++)
		fprintf(out, "%u,%s", b[i], (i % 24 == 23) ? "\n" : "");
	return 0;
}

static char *slurp(const char *path, size_t *len)
{
	FILE *fp = fopen(path, "rb");
	char *buf;
	long n;

	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = malloc((size_t)n + 1);
	if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(fp);
	if (buf)
		buf[n] = '\0';
	*len = (size_t)n;
	return buf;
}

int main(int argc, char **argv)
{
	lua_State *L = luaL_newstate();
	FILE *out;
	int i;

	if (argc < 2 || !(out = fopen(argv[1], "w"))) {
		fprintf(stderr, "usage: bcembed <out.c> <module>=<file> ...\n");
		return 2;
	}
	fputs("#include \"src/kernel/lua/embedded.h\"\n\n", out);
	for (i = 2; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		size_t len, n;
		char *src, *name, *chunk;
		int is_json;

		if (!eq) {
			fprintf(stderr, "bcembed: bad argument %s\n", argv[i]);
			return 2;
		}
		name = strndup(argv[i], (size_t)(eq - argv[i]));
		src = slurp(eq + 1, &len);
		if (!src) {
			fprintf(stderr, "bcembed: cannot read %s\n", eq + 1);
			return 1;
		}
		is_json = strlen(eq + 1) > 5 && !strcmp(eq + 1 + strlen(eq + 1) - 5, ".json");
		if (is_json) {
			/* return [=====[ ... ]=====] */
			chunk = malloc(len + 32);
			n = (size_t)sprintf(chunk, "return [=====[\n%s]=====]", src);
		} else {
			chunk = src;
			n = len;
		}
		{
			char chunkname[256];
			snprintf(chunkname, sizeof chunkname, "=%s", name);
			if (luaL_loadbuffer(L, chunk, n, chunkname) != 0) {
				fprintf(stderr, "bcembed: %s\n", lua_tostring(L, -1));
				return 1;
			}
		}
		fprintf(out, "static const unsigned char data%d[] = {\n", i);
		lua_dump(L, writer, out);
		fputs("0};\n", out);
		lua_pop(L, 1);
	}
	fputs("\nconst struct qwe_embedded qwe_embedded_modules[] = {\n", out);
	for (i = 2; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		fprintf(out, "\t{\"%.*s\", data%d, sizeof data%d - 1},\n", (int)(eq - argv[i]), argv[i], i, i);
	}
	fputs("\t{0, 0, 0},\n};\n", out);
	fclose(out);
	return 0;
}
