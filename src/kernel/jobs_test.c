#include "greatest.h"
#include "src/kernel/jobs.h"

#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>

/* Counts what qwe_jobs_load takes from strdup and calloc, which Lua does not use, so
 * "live" is exactly the blocks the jobs hold. */
void *__real_calloc(size_t a, size_t b);
char *__real_strdup(const char *s);
void __real_free(void *p);
void *__real_realloc(void *p, size_t n);

/* Allocation number fail_at (1-based, over calloc, strdup and realloc) returns NULL; 0 = never. */
static int fail_at, calls;

static int should_fail(void)
{
	return fail_at && ++calls == fail_at;
}

#define MAX_TRACKED 256
static void *tracked[MAX_TRACKED];
static int live;

static void track(void *p)
{
	int i;

	for (i = 0; p && i < MAX_TRACKED; i++)
		if (!tracked[i]) {
			tracked[i] = p;
			live++;
			return;
		}
}

void *__wrap_calloc(size_t a, size_t b)
{
	void *p = should_fail() ? NULL : __real_calloc(a, b);

	track(p);
	return p;
}

char *__wrap_strdup(const char *s)
{
	char *p = should_fail() ? NULL : __real_strdup(s);

	track(p);
	return p;
}

void *__wrap_realloc(void *p, size_t n)
{
	return should_fail() ? NULL : __real_realloc(p, n);
}

void __wrap_free(void *p)
{
	int i;

	for (i = 0; p && i < MAX_TRACKED; i++)
		if (tracked[i] == p) {
			tracked[i] = NULL;
			live--;
			break;
		}
	__real_free(p);
}

/* A workflow with two jobs, one needing the other, each with steps. */
static long load_two_jobs(lua_State *L, struct job **jobs)
{
	int rc = luaL_dostring(L,
	    "return { jobs = {"
	    "  build = { target = 'local', steps = { { run = 'true' }, { run = 'true' } } },"
	    "  test = { needs = { 'build' }, steps = { { run = 'true' } } } } }");

	if (rc)
		return -2;
	return qwe_jobs_load(L, "w.yaml", jobs);
}

/* The timeout of { timeout-seconds = seconds } in ms; -1 when the field is absent. */
static long convert(const double *seconds)
{
	lua_State *L = luaL_newstate();
	long ms;

	lua_newtable(L);
	if (seconds) {
		lua_pushnumber(L, *seconds);
		lua_setfield(L, -2, "timeout-seconds");
	}
	ms = qwe_timeout_ms_at(L, -1);
	lua_close(L);
	return ms;
}

TEST timeout_conversion_is_total(void)
{
	double one = 1, third = 0.3, max = QWE_TIMEOUT_MAX_SECONDS, tiny = 1e-9, sub_ms = 0.0004, huge = 1e30;

	ASSERT_EQ(0, convert(NULL)); /* absent: no limit */
	ASSERT_EQ(1000, convert(&one));
	ASSERT_EQ(300, convert(&third));
	ASSERT_EQ(604800L * 1000, convert(&max)); /* the largest the schema admits */
	/* A positive value is never rounded down to 0, which means "no limit". */
	ASSERT_EQ(1, convert(&tiny));
	ASSERT_EQ(1, convert(&sub_ms));
	/* The schema rejects this; the conversion stays defined if it is ever reached. */
	ASSERT_EQ(604800L * 1000, convert(&huge));
	PASS();
}

TEST jobs_free_releases_everything(void)
{
	lua_State *L = luaL_newstate();
	struct job *jobs = NULL;
	long n;
	int ref, ref0, ref1;

	n = load_two_jobs(L, &jobs);
	ASSERT_EQ(2, n);
	ASSERT(live > 0);
	lua_pop(L, 1); /* the workflow table */
	ref0 = jobs[0].ref;
	ref1 = jobs[1].ref;

	qwe_jobs_free(L, jobs, (size_t)n);

	ASSERT_EQ(0, live); /* ids, targets and needs */
	/* the jobs' registry refs went back to the registry, so a new ref reuses one */
	lua_newtable(L);
	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	ASSERT(ref == ref0 || ref == ref1);
	lua_close(L);
	PASS();
}

TEST jobs_free_handles_partial_load(void)
{
	lua_State *L = luaL_newstate();
	struct job *jobs = NULL;
	int ref, ref0;

	/* the job "a" loads whole; "b" has a step with neither run: nor uses: */
	ASSERT_EQ(0, luaL_dostring(L,
	    "return { jobs = {"
	    "  a = { needs = { 'b' }, steps = { { run = 'true' } } },"
	    "  b = { steps = { {} } } } }"));
	ASSERT_EQ(-1, qwe_jobs_load(L, "w.yaml", &jobs));
	ASSERT_EQ(0, live); /* the failed load left nothing behind */
	lua_pop(L, 1);
	lua_newtable(L);
	ref0 = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_newtable(L);
	ref = luaL_ref(L, LUA_REGISTRYINDEX);
	ASSERT(ref0 != ref);
	lua_close(L);
	PASS();
}

TEST jobs_load_fails_cleanly_when_any_allocation_fails(void)
{
	int at;

	for (at = 1; at < 64; at++) {
		lua_State *L = luaL_newstate();
		struct job *jobs = NULL;
		long n;

		fail_at = at;
		calls = 0;
		n = load_two_jobs(L, &jobs);
		fail_at = 0;
		if (n >= 0) { /* past the last allocation: the load succeeded */
			ASSERT(at > 3);
			qwe_jobs_free(L, jobs, (size_t)n);
			lua_close(L);
			break;
		}
		ASSERT_EQ_FMT(-1L, n, "%ld");
		ASSERT_EQ_FMT(0, live, "%d"); /* nothing left behind, at = */
		lua_close(L);
	}
	ASSERT(at < 64);
	PASS();
}

SUITE(jobs)
{
	RUN_TEST(jobs_load_fails_cleanly_when_any_allocation_fails);
	RUN_TEST(jobs_free_handles_partial_load);
	RUN_TEST(jobs_free_releases_everything);
	RUN_TEST(timeout_conversion_is_total);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(jobs);
	GREATEST_MAIN_END();
}
