#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/oom_shim.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static int sleep_case(void *arg)
{
	sleep(*(unsigned *)arg);
	return 7;
}

TEST probe_timeout_defaults_can_be_overridden(void)
{
	struct qwe_oom_outcome o;
	unsigned secs = 2;

	ASSERT_EQ(0, setenv("QWE_OOM_PROBE_TIMEOUT", "1", 1));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(!o.exited);
	ASSERT_EQ(SIGALRM, o.signal);

	ASSERT_EQ(0, setenv("QWE_OOM_PROBE_TIMEOUT", "3", 1));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(o.exited);
	ASSERT_EQ(7, o.code);

	ASSERT_EQ(0, unsetenv("QWE_OOM_PROBE_TIMEOUT"));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(probe_timeout_defaults_can_be_overridden);
	GREATEST_MAIN_END();
}
