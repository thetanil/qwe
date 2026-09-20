#define _GNU_SOURCE
#include "src/kernel/jobs.h"

#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The timeout of a table; 0 if it has none. */
long qwe_timeout_ms_at(lua_State *L, int idx)
{
	long ms = 0;

	lua_getfield(L, idx, "timeout-seconds");
	if (lua_isnumber(L, -1)) {
		double s = lua_tonumber(L, -1);

		/* Clamped before the cast, so it is always in range; 0 stays "none". */
		if (s > QWE_TIMEOUT_MAX_SECONDS)
			s = QWE_TIMEOUT_MAX_SECONDS;
		if (s > 0) {
			ms = (long)(s * 1000.0);
			if (ms < 1)
				ms = 1;
		}
	}
	lua_pop(L, 1);
	return ms;
}

static int cmp_job(const void *a, const void *b)
{
	return strcmp(((const struct job *)a)->id, ((const struct job *)b)->id);
}

/* Keys the engine accepts in the schema but does not act on yet. Running a
 * workflow that uses one would silently do the wrong thing, so it is refused. */
static const char *const unimplemented_job_keys[] = {NULL};
static const char *const unimplemented_step_keys[] = {NULL};

static void oom(const char *path)
{
	fprintf(stderr, "qwe run: %s: out of memory\n", path);
}

static int refuse(const char *path, const char *what, const char *id, const char *key, const char *why)
{
	fprintf(stderr, "qwe run: %s: %s %s: %s: %s\n", path, what, id, key, why);
	return -1;
}

/* Reads the jobs out of the decoded workflow (on top of L's stack) into a
 * list sorted by id, and refuses what the engine cannot run yet. Returns the
 * job count, or -1 after printing why. */
long qwe_jobs_load(lua_State *L, const char *path, struct job **out)
{
	struct job *jobs = NULL;
	size_t n = 0, cap = 0, i, k;
	int bad = 0, base = lua_gettop(L);

	lua_getfield(L, -1, "jobs");
	lua_pushnil(L);
	while (!bad && lua_next(L, -2)) {
		if (n == cap) {
			struct job *grown = realloc(jobs, (cap ? cap * 2 : 8) * sizeof *jobs);

			if (!grown)
				goto nomem;
			jobs = grown;
			cap = cap ? cap * 2 : 8;
		}
		memset(&jobs[n], 0, sizeof jobs[n]);
		jobs[n].run.timer.fd = jobs[n].run.step_timer.fd = jobs[n].run.grace_timer.fd = -1;
		jobs[n].run.proc.out_fd = -1;
		jobs[n].run.live_step = -1;
		jobs[n].run.outputs_ref = LUA_NOREF;
		jobs[n].ref = LUA_NOREF;
		n++; /* counted before it is filled, so a failure below frees it */
		jobs[n - 1].id = strdup(lua_tostring(L, -2));
		if (!jobs[n - 1].id)
			goto nomem;
		jobs[n - 1].ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the job table; the key stays */
	}
	lua_pop(L, 1); /* jobs */
	if (bad) {
		qwe_jobs_free(L, jobs, n);
		return -1;
	}
	qsort(jobs, n, sizeof *jobs, cmp_job);

	for (i = 0; i < n; i++) {
		struct job *j = &jobs[i];
		size_t nsteps;

		lua_rawgeti(L, LUA_REGISTRYINDEX, j->ref);
		lua_getfield(L, -1, "target");
		j->target = strdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : "local");
		if (!j->target)
			goto nomem;
		lua_pop(L, 1);
		for (k = 0; unimplemented_job_keys[k]; k++) {
			lua_getfield(L, -1, unimplemented_job_keys[k]);
			if (!lua_isnil(L, -1))
				bad = refuse(path, "job", j->id, unimplemented_job_keys[k], "not implemented yet");
			lua_pop(L, 1);
		}
		j->timeout_ms = qwe_timeout_ms_at(L, -1);
		lua_getfield(L, -1, "needs");
		if (lua_istable(L, -1)) {
			size_t nn = lua_objlen(L, -1);

			j->needs = calloc(nn ? nn : 1, sizeof *j->needs);
			if (!j->needs)
				goto nomem;
			j->nneeds = nn;
			for (k = 0; k < j->nneeds; k++) {
				lua_rawgeti(L, -1, (int)k + 1);
				j->needs[k] = strdup(lua_tostring(L, -1));
				if (!j->needs[k])
					goto nomem;
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
		lua_getfield(L, -1, "steps");
		nsteps = lua_objlen(L, -1);
		for (k = 1; k <= nsteps; k++) {
			size_t m;

			lua_rawgeti(L, -1, (int)k);
			lua_getfield(L, -1, "run");
			lua_getfield(L, -2, "uses");
			if (!lua_isstring(L, -2) && !lua_isstring(L, -1))
				bad = refuse(path, "job", j->id, "steps", "a step needs run: or uses:");
			lua_pop(L, 2);
			for (m = 0; unimplemented_step_keys[m]; m++) {
				lua_getfield(L, -1, unimplemented_step_keys[m]);
				if (!lua_isnil(L, -1))
					bad = refuse(path, "job", j->id, unimplemented_step_keys[m], "not implemented yet");
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 2); /* steps, job */
		j->nsteps = nsteps;
	}
	if (bad) {
		qwe_jobs_free(L, jobs, n);
		return -1;
	}
	*out = jobs;
	return (long)n;

nomem:
	oom(path);
	lua_settop(L, base);
	qwe_jobs_free(L, jobs, n);
	return -1;
}


/* Frees everything qwe_jobs_load and the run put into the jobs, and releases their
 * registry refs in L. Call it before lua_close(L). */
void qwe_jobs_free(lua_State *L, struct job *jobs, size_t n)
{
	size_t i, k;

	for (i = 0; i < n; i++) {
		struct job *j = &jobs[i];

		for (k = 0; k < j->nneeds; k++)
			free(j->needs[k]);
		free(j->needs);
		for (k = 0; k < j->nsteps && j->steps; k++) {
			free((char *)j->steps[k].id);
			free(j->steps[k].outputs_json);
		}
		free(j->steps);
		free(j->id);
		free(j->detail);
		free(j->target);
		luaL_unref(L, LUA_REGISTRYINDEX, j->ref);
	}
	free(jobs);
}
