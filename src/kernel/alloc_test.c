#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/alloc.h"
#include "src/kernel/oom_shim.h"
#include "src/testing/owned.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Runs body in a child with allocation failing, and returns what it wrote to
 * stderr and how it ended. */
static int run_failing(void (*body)(void), char *err, size_t errcap, int *status)
{
	int fd[2];
	ssize_t got;

	if (pipe(fd))
		return -1;
	pid_t pid = fork();
	if (pid == 0) {
		dup2(fd[1], 2);
		close(fd[0]);
		qwe_oom_arm(1);
		body();
		_exit(0);
	}
	close(fd[1]);
	got = read(fd[0], err, errcap - 1);
	err[got < 0 ? 0 : got] = 0;
	close(fd[0]);
	waitpid(pid, status, 0);
	return 0;
}

static void grab(void)
{
	(void)qwe_xmalloc(16);
}

TEST helper_aborts_and_names_the_site(void)
{
	char err[512];
	int status;

	ASSERT_EQ(0, run_failing(grab, err, sizeof err, &status));
	ASSERT(WIFSIGNALED(status));
	ASSERT_EQ(SIGABRT, WTERMSIG(status));
	ASSERT(strstr(err, "alloc_test.c:"));
	ASSERT(strstr(err, "out of memory"));
	PASS();
}

TEST helper_returns_the_block_when_memory_is_available(void)
{
	char *p = qwe_xstrdup("abc");

	ASSERT_STR_EQ("abc", p);
	p = qwe_xrealloc(p, 100);
	ASSERT_EQ('a', p[0]);
	free(p);
	p = qwe_xcalloc(4, 4);
	ASSERT_EQ(0, p[15]);
	free(p);
	PASS();
}

TEST shim_fails_the_nth_call(void)
{
	void *p;
	int i;

	/* transparent until armed */
	p = malloc(8);
	ASSERT(p);
	free(p);

	/* the 3rd allocation fails, in whichever call it is, and only that one */
	qwe_oom_arm(3);
	for (i = 1; i <= 5; i++) {
		switch (i % 4) {
		case 0: p = strdup("x"); break;
		case 1: p = calloc(1, 8); break;
		case 2: p = malloc(8); break;
		default: p = realloc(NULL, 8); break;
		}
		qwe_own(p);
		ASSERT_EQ_FMT(i != 3, p != NULL, "%d");
	}
	ASSERT_EQ(5, qwe_oom_count());
	ASSERT(qwe_oom_fired());

	/* disarmed, everything passes again */
	qwe_oom_arm(0);
	p = malloc(8);
	ASSERT(p);
	ASSERT(!qwe_oom_fired());
	free(p);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(helper_aborts_and_names_the_site);
	RUN_TEST(helper_returns_the_block_when_memory_is_available);
	RUN_TEST(shim_fails_the_nth_call);
	GREATEST_MAIN_END();
}
