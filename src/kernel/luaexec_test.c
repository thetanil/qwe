#define _POSIX_C_SOURCE 200809L
/* qwe.exec, the paths a plugin can reach with bad input or a starved system
 * (ticket quality/09); the happy paths are the e2e cases. */
#include "greatest.h"
#include "src/kernel/luavm.h"

#include <errno.h>
#include <lauxlib.h>
#include <string.h>
#include <sys/resource.h>
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
	lim.rlim_cur = (rlim_t)(first + 2); /* the first pipe fits, the second does not */
	setrlimit(RLIMIT_NOFILE, &lim);
	{
		int rc = luaL_dostring(L, "r1, r2 = exec.run({ 'true' })");

		setrlimit(RLIMIT_NOFILE, &old);
		ASSERT_EQ(0, rc);
	}
	lua_getglobal(L, "r1");
	ASSERT(lua_isnil(L, -1));
	lua_getglobal(L, "r2");
	ASSERT_STR_EQ(strerror(EMFILE), lua_tostring(L, -1));
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
		ASSERT_STR_EQ(strerror(EAGAIN), lua_tostring(L, -1));
	}
	lua_close(L);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(bad_arguments_raise);
	RUN_TEST(wait_gives_up_on_a_running_child);
	RUN_TEST(run_reports_a_failed_spawn);
	GREATEST_MAIN_END();
}
