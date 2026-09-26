/* The lua_atpanic handler registered in qwe_lua_new(): any Lua error that
 * escapes every pcall must come out as a normal qwe error line, never
 * LuaJIT's raw "PANIC: unprotected error in call to Lua API" text (ticket 18,
 * the incident that exposed the missing handler). The handler calls exit(),
 * so it is tripped in a forked child and the parent inspects the child's exit
 * code and captured stderr. */
#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/luavm.h"
#include "src/testing/env.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Reproduces the incident's shape: an error raised with no enclosing pcall
 * between it and the C API, via lua_call rather than lua_pcall. Never
 * returns: either the panic handler's exit() fires, or something is
 * badly wrong and the child exits 99 so the parent's assertions catch it. */
static void trip_panic(int errfd)
{
	lua_State *L = qwe_lua_new();

	dup2(errfd, STDERR_FILENO);
	if (!L)
		_exit(98);
	lua_getglobal(L, "error");
	lua_pushliteral(L, "boom");
	lua_call(L, 1, 0);
	_exit(99); /* unreached: lua_call above must not return */
}

TEST atpanic_formats_a_clean_error_and_exits_failed(void)
{
	int fds[2], status;
	char buf[512];
	ssize_t n;
	pid_t pid;

	ASSERT_EQ(0, pipe(fds));
	pid = fork();
	ASSERT(pid >= 0);
	if (pid == 0) {
		close(fds[0]);
		trip_panic(fds[1]);
	}
	close(fds[1]);
	n = read(fds[0], buf, sizeof buf - 1);
	close(fds[0]);
	ASSERT(n > 0);
	buf[n] = '\0';
	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT(WIFEXITED(status));
	ASSERT_EQ_FMT(1, WEXITSTATUS(status), "%d"); /* QWE_EXIT_FAILED (src/kernel/qwe.h) */
	ASSERT(strstr(buf, "qwe: internal error: boom") != NULL);
	ASSERT(strstr(buf, "PANIC") == NULL);
	PASS();
}

/* Without QWE_LUA_COVERAGE (the OOM tests unset it) a VM starts and flushes
 * with the Lua coverage hook off. */
TEST starts_and_flushes_with_lua_coverage_off(void)
{
	const char *was = getenv("QWE_LUA_COVERAGE");
	char *saved = was ? strdup(was) : NULL;
	lua_State *L = NULL;
	int ok = (!was || saved) && qwe_test_unsetenv("QWE_LUA_COVERAGE") == 0;

	if (ok)
		L = qwe_lua_new();
	if (L) {
		qwe_lua_coverage_flush(L);
		lua_close(L);
	}
	if (saved)
		(void)qwe_test_setenv("QWE_LUA_COVERAGE", saved);
	free(saved);
	ASSERT(ok);
	ASSERT(L != NULL);
	PASS();
}

SUITE(luavm)
{
	RUN_TEST(atpanic_formats_a_clean_error_and_exits_failed);
	RUN_TEST(starts_and_flushes_with_lua_coverage_off);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(luavm);
	GREATEST_MAIN_END();
}
