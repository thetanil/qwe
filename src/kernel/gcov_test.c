/* qwe_gcov_dump is a no-op outside `bazel coverage`. Inside it, it dumps only
 * with QWE_LUA_COVERAGE set (the OOM tests unset it). Its callers end in exec,
 * _exit or abort, so their own coverage never shows the dump running: this
 * process calls it both ways and then exits normally, which writes both. */
#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/gcov.h"

#include <stdlib.h>
#include <string.h>

TEST dump_runs_with_and_without_lua_coverage(void)
{
	const char *was = getenv("QWE_LUA_COVERAGE");
	char *saved = was ? strdup(was) : NULL;
	int ok = (!was || saved) && unsetenv("QWE_LUA_COVERAGE") == 0;

	if (ok) {
		qwe_gcov_dump();
		ok = setenv("QWE_LUA_COVERAGE", "1", 1) == 0;
		qwe_gcov_dump();
	}
	if (saved)
		ok = setenv("QWE_LUA_COVERAGE", saved, 1) == 0 && ok;
	else
		ok = unsetenv("QWE_LUA_COVERAGE") == 0 && ok;
	free(saved);
	ASSERT(ok);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(dump_runs_with_and_without_lua_coverage);
	GREATEST_MAIN_END();
}
