#include "greatest.h"
#include "src/kernel/alloc.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* malloc fails on demand; every other allocation passes through. */
void *__real_malloc(size_t n);
static int fail_malloc;

void *__wrap_malloc(size_t n)
{
	return fail_malloc ? NULL : __real_malloc(n);
}

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
		fail_malloc = 1;
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

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(helper_aborts_and_names_the_site);
	RUN_TEST(helper_returns_the_block_when_memory_is_available);
	GREATEST_MAIN_END();
}
