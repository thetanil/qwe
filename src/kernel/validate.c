#define _POSIX_C_SOURCE 200809L
#include "src/kernel/validate.h"

#include "src/kernel/dag.h"

#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct problem {
	struct qwe_pos pos;
	char *message;
};

struct problems {
	struct problem *v;
	size_t n, cap;
	int oom; /* something could not be recorded or checked for want of memory */
};

static void add(struct problems *ps, struct qwe_pos pos, const char *message)
{
	char *copy;

	if (ps->n == ps->cap) {
		size_t cap = ps->cap ? ps->cap * 2 : 16;
		struct problem *grown = realloc(ps->v, cap * sizeof *ps->v);

		if (!grown) {
			ps->oom = 1;
			return;
		}
		ps->v = grown;
		ps->cap = cap;
	}
	copy = strdup(message);
	if (!copy) {
		ps->oom = 1;
		return;
	}
	ps->v[ps->n].pos = pos;
	ps->v[ps->n].message = copy;
	ps->n++;
}

/* Finds the position an error should be reported at. A pointer with no entry
 * of its own (for example a missing key) falls back to its nearest ancestor. */
static struct qwe_pos locate(const struct qwe_positions *pos, const char *pointer, int want_key, struct problems *ps)
{
	char *p = strdup(pointer);
	struct qwe_pos out = {1, 1};
	int first = 1;

	if (!p) {
		ps->oom = 1;
		return out;
	}

	for (;;) {
		char *slash;

		if (first && want_key ? qwe_positions_key(pos, p, &out) == 0 : qwe_positions_value(pos, p, &out) == 0)
			break;
		first = 0;
		slash = strrchr(p, '/');
		if (!slash)
			break;
		*slash = '\0';
	}
	free(p);
	return out;
}

static char *escape_token(const char *s)
{
	char *out, *o;

	if (!s)
		s = "";
	out = malloc(strlen(s) * 2 + 1);
	o = out;

	if (!out)
		return NULL;

	for (; *s; s++) {
		if (*s == '~') {
			*o++ = '~';
			*o++ = '0';
		} else if (*s == '/') {
			*o++ = '~';
			*o++ = '1';
		} else {
			*o++ = *s;
		}
	}
	*o = '\0';
	return out;
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_problem(const void *a, const void *b)
{
	const struct problem *x = a, *y = b;

	if (x->pos.line != y->pos.line)
		return x->pos.line < y->pos.line ? -1 : 1;
	if (x->pos.col != y->pos.col)
		return x->pos.col < y->pos.col ? -1 : 1;
	return strcmp(x->message, y->message);
}

/* Runs the needs: graph checks on doc (at stack index doc). A failure to
 * allocate is recorded in ps->oom, and the check is not run. */
static void check_dag(lua_State *L, int doc, const struct qwe_positions *pos, struct problems *ps)
{
	char **ids = NULL;
	struct qwe_dag_job *jobs = NULL;
	size_t n = 0, cap = 0, i, k;
	struct qwe_dag_error err;
	enum qwe_dag_status st;
	int base = lua_gettop(L);
	char *tok = NULL, *ptr = NULL;

	lua_getfield(L, doc, "jobs");
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		lua_pop(L, 1);
		if (n == cap) {
			size_t ncap = cap ? cap * 2 : 8;
			char **grown = realloc(ids, ncap * sizeof *ids);

			if (!grown)
				goto nomem;
			ids = grown;
			cap = ncap;
		}
		ids[n] = strdup(lua_tostring(L, -1));
		if (!ids[n])
			goto nomem;
		n++;
	}
	if (n)
		qsort(ids, n, sizeof *ids, cmp_str);

	jobs = calloc(n ? n : 1, sizeof *jobs);
	if (!jobs)
		goto nomem;
	for (i = 0; i < n; i++) {
		size_t nn = 0;
		const char **needs = NULL;

		jobs[i].id = ids[i];
		lua_getfield(L, -1, ids[i]);
		lua_getfield(L, -1, "needs");
		if (lua_istable(L, -1)) {
			nn = lua_objlen(L, -1);
			needs = calloc(nn ? nn : 1, sizeof *needs);
			if (!needs)
				goto nomem;
			jobs[i].needs = needs;
			jobs[i].nneeds = nn;
			for (k = 0; k < nn; k++) {
				lua_rawgeti(L, -1, (int)k + 1);
				needs[k] = strdup(lua_tostring(L, -1));
				if (!needs[k])
					goto nomem;
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 2);
	}
	lua_pop(L, 1); /* jobs */

	st = qwe_dag_check(jobs, n, &err);
	if (st != QWE_DAG_OK) {
		/* a check that could not run is an error too, reported at the jobs it covers */
		tok = st == QWE_DAG_NO_MEMORY ? strdup("") : escape_token(jobs[err.job].id);
		if (!tok)
			goto nomem;
		ptr = malloc(strlen(tok) + 64);
		if (!ptr)
			goto nomem;
		if (st == QWE_DAG_NO_MEMORY)
			sprintf(ptr, "/jobs");
		else if (st == QWE_DAG_UNKNOWN_NEED)
			sprintf(ptr, "/jobs/%s/needs/%lu", tok, (unsigned long)err.need);
		else
			sprintf(ptr, "/jobs/%s", tok);
		add(ps, locate(pos, ptr, st == QWE_DAG_CYCLE, ps), err.message);
	}
	goto out;
nomem:
	ps->oom = 1;
out:
	lua_settop(L, base);
	free(tok);
	free(ptr);
	for (i = 0; jobs && i < n; i++) {
		for (k = 0; k < jobs[i].nneeds; k++)
			free((char *)jobs[i].needs[k]);
		free((void *)jobs[i].needs);
	}
	for (i = 0; i < n; i++)
		free(ids[i]);
	free(jobs);
	free(ids);
}


/* The directory the workflow is in, "" for the current one. */
static char *workflow_dir(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? strndup(path, (size_t)(slash - path)) : strdup("");
}

/* Prints the list of { where, message } at idx, the problems found in the
 * project's plugins, in the order the plugins were loaded. Returns how many. */
static size_t print_plugin_problems(lua_State *L, const char *cmd, int idx)
{
	size_t i, n = lua_objlen(L, idx);

	for (i = 1; i <= n; i++) {
		lua_rawgeti(L, idx, (int)i);
		lua_getfield(L, -1, "where");
		lua_getfield(L, -2, "message");
		fprintf(stderr, "%s: %s: %s\n", cmd, lua_tostring(L, -2), lua_tostring(L, -1));
		lua_pop(L, 3);
	}
	return n;
}

/* Adds the { pointer, kind, message } errors of the list at idx to ps. */
static void collect_errors(lua_State *L, int idx, const struct qwe_positions *pos, struct problems *ps)
{
	size_t i, n = lua_objlen(L, idx);

	for (i = 1; i <= n; i++) {
		const char *pointer, *kind, *message;

		lua_rawgeti(L, idx, (int)i);
		lua_getfield(L, -1, "pointer");
		lua_getfield(L, -2, "kind");
		lua_getfield(L, -3, "message");
		pointer = lua_tostring(L, -3);
		kind = lua_tostring(L, -2);
		message = lua_tostring(L, -1);
		add(ps, locate(pos, pointer ? pointer : "", kind && strcmp(kind, "key") == 0, ps),
		    message ? message : "invalid");
		lua_pop(L, 4);
	}
}

/* The last token of a JSON Pointer, unescaped, as a new string. */
static char *last_token(const char *pointer)
{
	const char *tok = strrchr(pointer, '/');
	char *out, *o;

	tok = tok ? tok + 1 : pointer;
	out = malloc(strlen(tok) + 1);
	if (!out)
		return NULL;
	for (o = out; *tok; tok++) {
		if (*tok == '~' && tok[1] == '1') {
			*o++ = '/';
			tok++;
		} else if (*tok == '~' && tok[1] == '0') {
			*o++ = '~';
			tok++;
		} else {
			*o++ = *tok;
		}
	}
	*o = '\0';
	return out;
}

static void add_duplicate(const char *pointer, struct qwe_pos first, struct qwe_pos second, void *ud)
{
	struct problems *ps = ud;
	char *key = last_token(pointer), message[512];

	if (!key) {
		ps->oom = 1;
		return;
	}

	snprintf(message, sizeof message, "duplicate key \"%s\" (first at line %u)", key, first.line);
	free(key);
	add(ud, second, message);
}

/* Adds an error at every repeated mapping key: the parse keeps only the last
 * of them, so nothing later could tell. */
static void collect_duplicates(const struct qwe_positions *pos, struct problems *ps)
{
	qwe_positions_duplicates(pos, add_duplicate, ps);
}

/* Prints the problems in source order and frees them. */
static void print_problems(struct problems *ps, const char *cmd, const char *path)
{
	size_t i;

	if (ps->n > 1) /* qsort of a null array is undefined, even for zero elements */
		qsort(ps->v, ps->n, sizeof *ps->v, cmp_problem);
	for (i = 0; i < ps->n; i++) {
		fprintf(stderr, "%s: %s:%u:%u: %s\n", cmd, path, ps->v[i].pos.line, ps->v[i].pos.col, ps->v[i].message);
		free(ps->v[i].message);
	}
	free(ps->v);
	if (ps->oom)
		fprintf(stderr, "%s: %s: out of memory while checking the workflow\n", cmd, path);
}

/* A Lua error that escaped the validator's own pcalls: a real bug, unless the
 * message is shaped like a real allocator failure (lrexlib's "malloc failed",
 * a PCRE2 error string that mentions memory, or LuaJIT's own "not enough
 * memory"), in which case it is reported the same way as every other
 * out-of-memory path here rather than as "internal error" -- schema
 * compilation (kernel_validator, ADR-0009) now runs every plugin's
 * `pattern:` through PCRE2 on every call, so this is reachable, not just
 * theoretical. */
static void report_internal_error(lua_State *L, const char *cmd, const char *label, const char *path)
{
	const char *msg = lua_tostring(L, -1);

	if (msg && (strstr(msg, "malloc failed") || strstr(msg, "memory")))
		fprintf(stderr, "%s: %s: out of memory while checking the workflow\n", cmd, path);
	else
		fprintf(stderr, "%s: %s: internal error in the %s: %s\n", cmd, path, label, msg ? msg : "unknown error");
}

int qwe_validate_doc(lua_State *L, const char *cmd, const char *path, const struct qwe_positions *pos)
{
	struct problems ps = {0};
	int doc = lua_gettop(L);
	size_t n, nplugin;
	char *dir;

	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.validate");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto internal;
	lua_getfield(L, -1, "validate_project");
	lua_pushvalue(L, doc);
	dir = workflow_dir(path);
	if (!dir) {
		fprintf(stderr, "%s: %s: out of memory\n", cmd, path);
		lua_settop(L, doc);
		return 1;
	}
	lua_pushstring(L, dir);
	free(dir);
	if (lua_pcall(L, 2, 2, 0) != 0)
		goto internal;
	nplugin = print_plugin_problems(L, cmd, lua_gettop(L));
	lua_pop(L, 1); /* the plugin problems; the workflow's errors are on top */

	collect_errors(L, lua_gettop(L), pos, &ps);
	collect_duplicates(pos, &ps);
	lua_pop(L, 2); /* the list and the module */

	if (ps.n == 0 && !ps.oom)
		check_dag(L, doc, pos, &ps);

	n = ps.n + (size_t)ps.oom;
	print_problems(&ps, cmd, path);
	return (int)(n + nplugin);

internal:
	report_internal_error(L, cmd, "validator", path);
	lua_settop(L, doc);
	return 1;
}

int qwe_validate_inventory(lua_State *L, const char *cmd, const char *path, const struct qwe_positions *pos)
{
	struct problems ps = {0};
	int inv = lua_gettop(L);
	size_t n;

	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.inventory");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto internal;
	lua_getfield(L, -1, "validate");
	lua_pushvalue(L, inv);
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto internal;
	collect_errors(L, lua_gettop(L), pos, &ps);
	collect_duplicates(pos, &ps);
	lua_pop(L, 2); /* the list and the module */
	n = ps.n + (size_t)ps.oom;
	print_problems(&ps, cmd, path);
	return (int)n;

internal:
	report_internal_error(L, cmd, "inventory reader", path);
	lua_settop(L, inv);
	return 1;
}
