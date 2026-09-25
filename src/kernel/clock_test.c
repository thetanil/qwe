/* qwe_mono_now: the clock, and the abort when the kernel cannot read it. The
 * failure is one Linux cannot produce, so the test links with
 * --wrap=clock_gettime and fails the call itself. */
#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/clock.h"
#include "src/kernel/gcov.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int __real_clock_gettime(clockid_t clk, struct timespec *ts);

static int fail_clock;

int __wrap_clock_gettime(clockid_t clk, struct timespec *ts)
{
	if (fail_clock) {
		errno = EINVAL;
		return -1;
	}
	return __real_clock_gettime(clk, ts);
}

/* The child ends in abort(), which writes no coverage: dump it on the way out.
 * abort() still ends the process with SIGABRT once this returns. */
static void dump_on_abort(int sig)
{
	(void)sig;
	qwe_gcov_dump();
}

TEST reads_a_clock_that_does_not_go_back(void)
{
	struct timespec a = qwe_mono_now(), b = qwe_mono_now();

	ASSERT(b.tv_sec > a.tv_sec || (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));
	PASS();
}

TEST a_clock_that_fails_aborts(void)
{
	int status;
	pid_t pid = fork();

	ASSERT(pid >= 0);
	if (pid == 0) {
		struct sigaction sa;

		memset(&sa, 0, sizeof sa);
		sa.sa_handler = dump_on_abort;
		(void)sigaction(SIGABRT, &sa, NULL);
		fail_clock = 1;
		(void)qwe_mono_now();
		_exit(0);
	}
	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT(WIFSIGNALED(status));
	ASSERT_EQ(SIGABRT, WTERMSIG(status));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(reads_a_clock_that_does_not_go_back);
	RUN_TEST(a_clock_that_fails_aborts);
	GREATEST_MAIN_END();
}
