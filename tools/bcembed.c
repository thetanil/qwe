/* Compiles Lua files to LuaJIT bytecode and writes them as a C table, so they
 * can be linked into the qwe binary. Runs on the build host, at build time.
 *
 * usage: bcembed <out.c> <module>=<file> ...
 *
 * A file ending in .json is not code: it is embedded as a module that returns
 * its text as one string.
 *
 * Allocation failures abort through qwe_xmalloc and qwe_xrealloc (alloc.h, policy 2): a
 * build tool has nothing to recover to, and the message names the allocation. */
#include "src/kernel/alloc.h"
#include "src/kernel/errstr.h"
#include "src/kernel/put.h"

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

/* Reads the whole stream. No size is taken first, so nothing can change between sizing and
 * reading (CERT FIO19-C: no fseek/ftell to size a file; FIO45-C: no check-then-use on a
 * file): a file that grows or shrinks meanwhile is simply read as it is when read. More than
 * BCEMBED_MAX_SOURCE bytes is EFBIG, a failed read keeps its errno. The buffer grows by
 * doubling through qwe_xrealloc, one byte past the cap at most, and always has room for the
 * NUL. */
static char *slurp(const char *path, size_t *len)
{
	FILE *fp = fopen(path, "rb");
	char *buf = NULL;
	size_t n = 0, cap = 0, want, got;
	int err;

	if (!fp)
		return NULL;
	errno = 0; /* so a failed read's errno is its own */
	for (;;) {
		if (n == cap) {
			cap = cap ? 2 * cap : 4096;
			if (cap > (size_t)BCEMBED_MAX_SOURCE + 1)
				cap = (size_t)BCEMBED_MAX_SOURCE + 1;
			buf = qwe_xrealloc(buf, cap + 1);
		}
		want = cap - n;
		got = fread(buf + n, 1, want, fp);
		n += got;
		/* a short read is end of file or an error: the stream is not read again */
		if (got < want || n > (size_t)BCEMBED_MAX_SOURCE)
			break;
	}
	err = ferror(fp) ? (errno ? errno : EIO) : 0;
	(void)fclose(fp); /* fp is read-only: a failed fclose loses nothing */
	if (!err && n > (size_t)BCEMBED_MAX_SOURCE)
		err = EFBIG;
	if (err) {
		free(buf);
		errno = err;
		return NULL;
	}
	buf[n] = '\0';
	*len = n;
	return buf;
}

int main(int argc, char **argv)
{
	lua_State *L = luaL_newstate();
	FILE *out;
	int i, bad;

	out = argc < 2 ? NULL : fopen(argv[1], "w");
	if (!out) {
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
		n = (size_t)(eq - argv[i]);
		name = qwe_xmalloc(n + 1);
		memcpy(name, argv[i], n);
		name[n] = '\0';
		src = slurp(eq + 1, &len);
		if (!src) {
			qwe_diag("bcembed: cannot read %s: %s\n", eq + 1, qwe_strerror(errno));
			free(name);
			(void)fclose(out); /* failing already */
			return 1;
		}
		is_json = strlen(eq + 1) > 5 && !strcmp(eq + 1 + strlen(eq + 1) - 5, ".json");
		if (is_json) {
			/* return [=====[ ... ]=====]: copied, not formatted, so every byte of the
			 * text is kept (a %s would stop at a NUL) and nothing can be cut short. */
			static const char head[] = "return [=====[\n", tail[] = "]=====]";

			n = sizeof head - 1 + len + sizeof tail - 1;
			chunk = qwe_xmalloc(n);
			memcpy(chunk, head, sizeof head - 1);
			memcpy(chunk + sizeof head - 1, src, len);
			memcpy(chunk + sizeof head - 1 + len, tail, sizeof tail - 1);
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
