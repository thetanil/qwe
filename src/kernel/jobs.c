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

	lua_getfield(L, idx, "timeout-minutes");
	if (lua_isnumber(L, -1))
		ms = (long)(lua_tonumber(L, -1) * 60000.0);
	lua_pop(L, 1);
	return ms > 0 ? ms : 0;
}

static int cmp_job(const void *a, const void *b)
{
	return strcmp(((const struct job *)a)->id, ((const struct job *)b)->id);
}

/* Keys the engine accepts in the schema but does not act on yet. Running a
 * workflow that uses one would silently do the wrong thing, so it is refused. */
static const char *const unimplemented_job_keys[] = {NULL};
static const char *const unimplemented_step_keys[] = {"secret-outputs", NULL};

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
	int bad = 0;

	lua_getfield(L, -1, "jobs");
	lua_pushnil(L);
	while (!bad && lua_next(L, -2)) {
		if (n == cap)
			jobs = realloc(jobs, (cap = cap ? cap * 2 : 8) * sizeof *jobs);
		memset(&jobs[n], 0, sizeof jobs[n]);
		jobs[n].run.timer.fd = jobs[n].run.step_timer.fd = jobs[n].run.grace_timer.fd = -1;
		jobs[n].run.proc.out_fd = -1;
		jobs[n].run.live_step = -1;
		jobs[n].run.outputs_ref = LUA_NOREF;
		lua_pushvalue(L, -2);
		jobs[n].id = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
		jobs[n].ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the job table; the key stays */
		n++;
	}
	lua_pop(L, 1); /* jobs */
	if (bad)
		return -1;
	qsort(jobs, n, sizeof *jobs, cmp_job);

	for (i = 0; i < n; i++) {
		struct job *j = &jobs[i];
		size_t nsteps;

		lua_rawgeti(L, LUA_REGISTRYINDEX, j->ref);
		lua_getfield(L, -1, "target");
		j->target = strdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : "local");
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
			j->nneeds = lua_objlen(L, -1);
			j->needs = calloc(j->nneeds ? j->nneeds : 1, sizeof *j->needs);
			for (k = 0; k < j->nneeds; k++) {
				lua_rawgeti(L, -1, (int)k + 1);
				j->needs[k] = strdup(lua_tostring(L, -1));
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
	if (bad)
		return -1;
	*out = jobs;
	return (long)n;
}

