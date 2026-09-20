#include "greatest.h"
#include "src/kernel/jobs.h"

#include <lauxlib.h>

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

SUITE(jobs)
{
	RUN_TEST(timeout_conversion_is_total);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(jobs);
	GREATEST_MAIN_END();
}
