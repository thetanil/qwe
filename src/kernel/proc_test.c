#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/proc.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static char **argv_true(void *arg)
{
	static char *argv[] = {"sh", "-c", "exit 0", NULL};
	(void)arg;
	return argv;
}

TEST child_is_group_leader(void)
{
	struct qwe_proc p;
	sigset_t set, old;
	int st;

	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &old);

	ASSERT_EQ(0, qwe_proc_spawn(&p, argv_true, NULL));
	ASSERT(p.pid > 0);
	/* Checked before the child is reaped: a zombie still has its pgid. */
	ASSERT_EQ(p.pid, getpgid(p.pid));
	ASSERT(getpgid(p.pid) != getpgrp());

	ASSERT_EQ(p.pid, waitpid(p.pid, &st, 0));
	ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	close(p.out_fd);
	sigprocmask(SIG_SETMASK, &old, NULL);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(child_is_group_leader);
	GREATEST_MAIN_END();
}
