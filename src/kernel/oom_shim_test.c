#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/kernel/gcov.h"
#include "src/kernel/oom_shim.h"
#include "src/testing/env.h"

#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct saved_env {
	char *value;
	int had_value;
};

static void restore_timeout_env(void *arg)
{
	struct saved_env *saved = arg;

	if (saved->had_value)
		(void)qwe_test_setenv("QWE_OOM_PROBE_TIMEOUT", saved->value);
	else
		(void)qwe_test_unsetenv("QWE_OOM_PROBE_TIMEOUT");
	free(saved->value);
	saved->value = NULL;
	saved->had_value = 0;
}

static int sleep_case(void *arg)
{
	struct timespec ts = {.tv_sec = *(unsigned *)arg};

	nanosleep(&ts, NULL);
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

	ASSERT_EQ(0, qwe_test_setenv("QWE_OOM_PROBE_TIMEOUT", "1"));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(!o.exited);
	ASSERT_EQ(SIGALRM, o.signal);

	ASSERT_EQ(0, qwe_test_setenv("QWE_OOM_PROBE_TIMEOUT", "3"));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(o.exited);
	ASSERT_EQ(7, o.code);

	secs = 1;
	ASSERT_EQ(0, qwe_test_setenv("QWE_OOM_PROBE_TIMEOUT", "bogus"));
	ASSERT_EQ(0, qwe_oom_probe(0, 0, sleep_case, &secs, &o));
	ASSERT(o.exited);
	ASSERT_EQ(7, o.code);
	PASS();
}

/* The first line of path into line; 0 if there is one. */
static int first_line(const char *path, char *line, int cap)
{
	FILE *fp = fopen(path, "r");
	int got;

	if (!fp)
		return -1;
	got = fgets(line, cap, fp) != NULL;
	(void)fclose(fp);
	return got ? 0 : -1;
}

/* A process forked after qwe_oom_arm_child fails its own nth allocation, counted
 * from its first, and records it in the child log and, as an address, in the
 * site log. */
TEST forked_child_fails_its_nth_allocation_and_logs_it(void)
{
	const char *tmp = getenv("TEST_TMPDIR");
	char childlog[512], sitelog[512], line[64];
	int status;
	pid_t pid;

	qwe_xfmt(childlog, sizeof childlog, "%s/child.log", tmp ? tmp : "/tmp");
	qwe_xfmt(sitelog, sizeof sitelog, "%s/site.log", tmp ? tmp : "/tmp");
	(void)unlink(childlog);
	(void)unlink(sitelog);
	ASSERT_EQ(0, qwe_test_setenv("QWE_OOM_SITE_LOG", sitelog));
	qwe_oom_child_log(childlog);
	qwe_oom_arm_child(2);
	pid = fork();
	ASSERT(pid >= 0);
	if (pid == 0) {
		void *a = malloc(8), *b = malloc(8);
		int ok = a != NULL && b == NULL;

		free(a);
		qwe_oom_arm_child(0); /* the dump allocates */
		qwe_gcov_dump();
		_exit(ok ? 0 : 1);
	}
	qwe_oom_arm_child(0);
	qwe_oom_child_log(NULL);
	(void)qwe_test_unsetenv("QWE_OOM_SITE_LOG");
	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERTm("the child's 2nd allocation, and only that one, fails", WIFEXITED(status) && WEXITSTATUS(status) == 0);

	ASSERT_EQ(0, first_line(childlog, line, sizeof line));
	ASSERT_STR_EQ("x\n", line);
	ASSERT_EQ(0, first_line(sitelog, line, sizeof line));
	ASSERT(strspn(line, "0123456789abcdef") > 0);
	ASSERT_EQ('\n', line[strspn(line, "0123456789abcdef")]);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(probe_timeout_defaults_can_be_overridden);
	RUN_TEST(forked_child_fails_its_nth_allocation_and_logs_it);
	GREATEST_MAIN_END();
}
