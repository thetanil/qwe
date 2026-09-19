#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/proc.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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

static char **argv_two_sleeps(void *arg)
{
	static char *argv[] = {"sh", "-c", "sleep 1000 & sleep 1000", NULL};
	(void)arg;
	return argv;
}

/* Counts live (non-zombie) processes whose process group is pgid, by reading
 * /proc. A zombie is already dead; it only waits to be reaped. */
static int live_in_group(pid_t pgid)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int n = 0;

	while (d && (e = readdir(d)) != NULL) {
		char path[64], buf[512], *p;
		FILE *fp;
		int pid;

		if (sscanf(e->d_name, "%d", &pid) != 1)
			continue;
		snprintf(path, sizeof path, "/proc/%d/stat", pid);
		if (!(fp = fopen(path, "r")))
			continue;
		if (fgets(buf, sizeof buf, fp) && (p = strrchr(buf, ')')) != NULL) {
			char state;
			int ppid, pg;
			if (sscanf(p + 1, " %c %d %d", &state, &ppid, &pg) == 3 && pg == (int)pgid && state != 'Z')
				n++;
		}
		fclose(fp);
	}
	if (d)
		closedir(d);
	return n;
}

static void sleep_ms(long ms)
{
	struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
	nanosleep(&ts, NULL);
}

TEST group_kill_no_orphans(void)
{
	struct qwe_proc p;
	sigset_t set, old;
	int st, i;

	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &old);

	ASSERT_EQ(0, qwe_proc_spawn(&p, argv_two_sleeps, NULL));
	/* Wait for the shell to have started both sleeps: the group has 3 members
	 * (or 2 if the shell exec'd its last command). */
	for (i = 0; i < 200 && live_in_group(p.pid) < 2; i++)
		sleep_ms(10);
	ASSERT(live_in_group(p.pid) >= 2);

	/* One signal to the group, not to the pid... */
	ASSERT_EQ(0, qwe_proc_kill_group(&p, SIGTERM));
	ASSERT_EQ(p.pid, waitpid(p.pid, &st, 0));
	ASSERT(WIFSIGNALED(st));

	/* ...and nothing is left, including the background sleep. */
	for (i = 0; i < 300 && live_in_group(p.pid) > 0; i++)
		sleep_ms(10);
	ASSERT_EQ(0, live_in_group(p.pid));

	close(p.out_fd);
	sigprocmask(SIG_SETMASK, &old, NULL);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(child_is_group_leader);
	RUN_TEST(group_kill_no_orphans);
	GREATEST_MAIN_END();
}
