#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/oom_shim.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct saved_env {
	char *value;
	int had_value;
};

static void restore_timeout_env(void *arg)
{
	struct saved_env *saved = arg;

	if (saved->had_value)
		(void)setenv("QWE_OOM_PROBE_TIMEOUT", saved->value, 1);
	else
		(void)unsetenv("QWE_OOM_PROBE_TIMEOUT");
	free(saved->value);
	saved->value = NULL;
	saved->had_value = 0;
}

static int sleep_case(void *arg)
{
	sleep(*(unsigned *)arg);
	return 7;
}

TEST probe_timeout_defaults_can_be_overridden(void)
{
	struct qwe_oom_outcome o;
	/* static: greatest runs the teardown after this function has returned */
	static struct saved_env saved = {0};
	unsigned secs = 2;

	saved.value = getenv("QWE_OOM_PROBE_TIMEOUT");
	saved.had_value = saved.value != NULL;
	if (saved.had_value) {
		saved.value = strdup(saved.value);
		ASSERT(saved.value != NULL);
	}
	SET_TEARDOWN(restore_timeout_env, &saved);

	ASSERT_EQ(0, setenv("QWE_OOM_PROBE_TIMEOUT", "1", 1));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(!o.exited);
	ASSERT_EQ(SIGALRM, o.signal);

	ASSERT_EQ(0, setenv("QWE_OOM_PROBE_TIMEOUT", "3", 1));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(o.exited);
	ASSERT_EQ(7, o.code);

	secs = 1;
	ASSERT_EQ(0, setenv("QWE_OOM_PROBE_TIMEOUT", "bogus", 1));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(o.exited);
	ASSERT_EQ(7, o.code);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(probe_timeout_defaults_can_be_overridden);
	GREATEST_MAIN_END();
}
