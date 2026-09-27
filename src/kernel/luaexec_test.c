#define _POSIX_C_SOURCE 200809L
/* qwe.exec, the paths a plugin can reach with bad input or a starved system
 * (ticket quality/09); the happy paths are the e2e cases. */
#include "greatest.h"
#include "src/kernel/luavm.h"
#include "src/kernel/errstr.h"
#include "src/kernel/luaexec_testhook.h"

#include <errno.h>
#include <lauxlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static enum greatest_test_res run_lua(lua_State *L, const char *chunk)
{
	if (luaL_loadstring(L, chunk) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
		GREATEST_FAILm(lua_tostring(L, -1));
	}
	return GREATEST_TEST_RES_PASS;
}

#define LUA(L, chunk)                                                     \
	do {                                                              \
		enum greatest_test_res r_ = run_lua(L, chunk);            \
		if (r_ != GREATEST_TEST_RES_PASS)                         \
			return r_;                                        \
	} while (0)

TEST bad_arguments_raise(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local exec = require('qwe.exec')\n"
	    "local ok, err = pcall(exec.run, {})\n"
	    "assert(not ok and err:find('empty argv', 1, true), tostring(err))\n"
	    "-- preamble: sorted by name whatever the table order, and a bad name is named\n"
	    "local p = exec.preamble({ ZED = '1', ALPHA = '2', MID = '3', BETA = '4', OMEGA = '5' })\n"
	    "local order = {}\n"
	    "for name in p:gmatch('([A-Z]+)=') do order[#order + 1] = name end\n"
	    "assert(table.concat(order, ',') == 'ALPHA,BETA,MID,OMEGA,ZED', table.concat(order, ','))\n"
	    "for i = 1, 20 do -- past the initial capacity\n"
	    "  local t = {}\n"
	    "  for j = 1, 12 do t['V' .. j] = 'x' end\n"
	    "  assert(exec.preamble(t):find('V9=', 1, true))\n"
	    "end\n"
	    "local ok2, err2 = pcall(exec.preamble, { ['1bad'] = 'x' })\n"
	    "assert(not ok2 and err2:find(\"env variable '1bad': not a valid name\", 1, true), tostring(err2))\n");
	lua_close(L);
	PASS();
}

/* The repro from ticket sca-round3/02: exec_run and exec_preamble allocate C
 * memory (argv's strdup'd elements; preamble's names/values arrays) and then
 * called a Lua API function that raises on a wrong-typed argument, longjmping
 * past the frees. Fixed by validating every element first (nothing yet
 * allocated when the raise happens) -- see luaexec.c. Run under
 * --config=asan or --config=valgrind, this is the leak check: LeakSanitizer
 * or valgrind's leak-check names exec_run/exec_preamble on the old code and
 * is silent on the fix. */
TEST wrong_typed_arguments_do_not_leak(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local exec = require('qwe.exec')\n"
	    "-- exec.preamble({ A = {} }): a table value, not a string\n"
	    "local ok, err = pcall(exec.preamble, { A = {} })\n"
	    "assert(not ok and err:find('must both be strings', 1, true), tostring(err))\n"
	    "-- exec.preamble with a non-string key\n"
	    "local ok2, err2 = pcall(exec.preamble, { [true] = 'x' })\n"
	    "assert(not ok2 and err2:find('must both be strings', 1, true), tostring(err2))\n"
	    "-- exec.run({ \"true\", {} }): a table element in argv\n"
	    "local ok3, err3 = pcall(exec.run, { 'true', {} })\n"
	    "assert(not ok3, 'a table argv element must raise')\n"
	    "-- exec.run({ \"true\", 5 }): a number element is allowed, coerced\n"
	    "local code = exec.run({ 'true', 5 })\n"
	    "assert(code == 0, code)\n");
	lua_close(L);
	PASS();
}

/* exec_run's two post-fork pushes (its captured stdout, then stderr) run
 * after the child is forked, its pipes closed and it is reaped -- so by then
 * a raise there must free the two C buffers without leaking a descriptor or
 * leaving a zombie (ticket sca-round3/02). qwe_exec_run_test_force_raise
 * forces exactly that raise, since there is no cheap way to make a genuine
 * Lua allocation fail at that point (see luaexec_testhook.h). */
TEST forced_raise_after_fork_leaks_nothing(void)
{
	int which;

	for (which = 1; which <= 2; which++) {
		lua_State *L = qwe_lua_new();
		int before, after;
		pid_t reaped;

		ASSERT(L != NULL);
		before = dup(0);
		ASSERT(before >= 0);
		close(before);

		qwe_exec_run_test_force_raise(which);
		LUA(L,
		    "local exec = require('qwe.exec')\n"
		    "local ok, err = pcall(exec.run, { 'true' })\n"
		    "assert(not ok and err:find('test-forced raise', 1, true), tostring(err))\n");
		qwe_exec_run_test_force_raise(0);
		lua_close(L);

		after = dup(0);
		ASSERT(after >= 0);
		close(after);
		ASSERT_EQ_FMT(before, after, "%d"); /* no descriptor leaked */

		reaped = waitpid(-1, NULL, WNOHANG);
		ASSERT(reaped <= 0); /* nothing left unreaped, ours or otherwise */
	}
	PASS();
}

TEST wait_gives_up_on_a_running_child(void)
{
	lua_State *L = qwe_lua_new();

	ASSERT(L != NULL);
	LUA(L,
	    "local exec = require('qwe.exec')\n"
	    "local pid = assert(exec.run({ 'sleep', '30' }, nil, { background = true }))\n"
	    "assert(exec.wait(pid, 0.02) == false, 'still running after the bound')\n"
	    "exec.run({ 'kill', tostring(pid) })\n"
	    "assert(exec.wait(pid, 5) == true, 'gone once killed')\n");
	lua_close(L);
	PASS();
}

/* run returns nil and the reason, and leaves no descriptor open, when it cannot
 * make its pipes or fork. */
TEST run_reports_a_failed_spawn(void)
{
	lua_State *L = qwe_lua_new();
	struct rlimit lim, old;
	int first;

	ASSERT(L != NULL);
	ASSERT_EQ(0, luaL_dostring(L, "exec = require('qwe.exec')"));

	first = dup(0);
	close(first);
	getrlimit(RLIMIT_NOFILE, &old);
	lim = old;
	lim.rlim_cur = (rlim_t)first + 2; /* the first pipe fits, the second does not */
	setrlimit(RLIMIT_NOFILE, &lim);
	{
		int rc = luaL_dostring(L, "r1, r2 = exec.run({ 'true' })");

		setrlimit(RLIMIT_NOFILE, &old);
		ASSERT_EQ(0, rc);
	}
	lua_getglobal(L, "r1");
	ASSERT(lua_isnil(L, -1));
	lua_getglobal(L, "r2");
	ASSERT_STR_EQ(qwe_strerror(EMFILE), lua_tostring(L, -1));
	lua_settop(L, 0);
	ASSERT_EQ(first, dup(0)); /* nothing leaked */
	close(first);

	if (geteuid() != 0) { /* root ignores the process limit */
		int rc;

		getrlimit(RLIMIT_NPROC, &old);
		lim = old;
		lim.rlim_cur = 1;
		setrlimit(RLIMIT_NPROC, &lim);
		rc = luaL_dostring(L, "f1, f2 = exec.run({ 'true' })");
		setrlimit(RLIMIT_NPROC, &old);
		ASSERT_EQ(0, rc);
		lua_getglobal(L, "f1");
		ASSERT(lua_isnil(L, -1));
		lua_getglobal(L, "f2");
		ASSERT_STR_EQ(qwe_strerror(EAGAIN), lua_tostring(L, -1));
	}
	lua_close(L);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(bad_arguments_raise);
	RUN_TEST(wrong_typed_arguments_do_not_leak);
	RUN_TEST(forced_raise_after_fork_leaks_nothing);
	RUN_TEST(wait_gives_up_on_a_running_child);
	RUN_TEST(run_reports_a_failed_spawn);
	GREATEST_MAIN_END();
}
