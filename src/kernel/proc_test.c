#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/proc.h"
#include "src/kernel/luavm.h"
#include "src/testing/owned.h"

#include <dirent.h>
#include <errno.h>
#include <lauxlib.h>
#include <lua.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static char **argv_true(void *arg, int result_fd)
{
	static char *argv[] = {"sh", "-c", "exit 0", NULL};
	(void)arg;
	(void)result_fd;
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

/* Fields 5 and 6 (pgrp, session) of a /proc/<pid>/stat line. */
static int stat_group_session(const char *line, long *pgrp, long *sid)
{
	const char *p = strrchr(line, ')');
	long ppid;
	char state;

	return p && sscanf(p + 1, " %c %ld %ld %ld", &state, &ppid, pgrp, sid) == 4 ? 0 : -1;
}

static char **argv_sleep_long(void *arg, int result_fd)
{
	static char *argv[] = {"sleep", "1000", NULL};
	(void)arg;
	(void)result_fd;
	return argv;
}

/* The ssh ControlMaster is started with qwe.exec.run detach = true. Killing a
 * step's group (a cancel) must not reach it: it is in no step's process group,
 * nor even in qwe's own session. */
TEST master_outside_step_groups(void)
{
	struct qwe_proc step;
	sigset_t set, old;
	lua_State *L = qwe_lua_new();
	char path[] = "/tmp/qwe-master-XXXXXX", line[512] = "", src[512];
	long master_pg = -1, master_sid = -1, own_sid = getsid(0);
	int fd, st;
	FILE *fp;

	ASSERT(L != NULL);
	fd = mkstemp(path);
	ASSERT(fd >= 0);
	close(fd);
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &old);
	ASSERT_EQ(0, qwe_proc_spawn(&step, argv_sleep_long, NULL));

	/* the stand-in master records its own /proc stat, then exits */
	snprintf(src, sizeof src,
		"local exec = require(\"qwe.exec\")\n"
		"local code = exec.run({ \"sh\", \"-c\", \"cat /proc/self/stat > %s\" }, nil, { detach = true })\n"
		"assert(code == 0, code)\n",
		path);
	ASSERT_EQ(0, luaL_dostring(L, src));
	sigprocmask(SIG_SETMASK, &old, NULL);
	qwe_proc_kill_group(&step, SIGKILL);
	waitpid(step.pid, &st, 0);
	close(step.out_fd);
	lua_close(L);
	fp = qwe_own_file(fopen(path, "r"));
	ASSERT(fp != NULL);
	ASSERT(fgets(line, sizeof line, fp) != NULL);
	unlink(path);
	ASSERT_EQ(0, stat_group_session(line, &master_pg, &master_sid));
	ASSERT(master_pg != step.pid);
	ASSERT(master_pg != getpgrp());
	ASSERT(master_sid != own_sid);
	PASS();
}

static char **argv_two_sleeps(void *arg, int result_fd)
{
	static char *argv[] = {"sh", "-c", "sleep 1000 & sleep 1000", NULL};
	(void)arg;
	(void)result_fd;
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

/* The parent pid of pid, from /proc/<pid>/stat (field 4, after the comm in parentheses), or -1. */
static pid_t getppid_of(pid_t pid)
{
	char path[64], buf[512], *p;
	FILE *f;
	int ppid = -1;

	snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
	f = fopen(path, "r");
	if (!f)
		return -1;
	if (fgets(buf, sizeof buf, f) && (p = strrchr(buf, ')')) != NULL) {
		char state;

		if (sscanf(p + 1, " %c %d", &state, &ppid) != 2)
			ppid = -1;
	}
	fclose(f);
	return ppid;
}

static char **argv_orphan(void *arg, int result_fd)
{
	/* The leader starts a background sleep, prints its pid and exits: the
	 * sleep is an orphan in the leader's group. */
	static char *argv[] = {"sh", "-c", "sleep 1000 & echo $!; exit 0", NULL};
	(void)arg;
	(void)result_fd;
	return argv;
}

TEST subreaper_reaps_orphans(void)
{
	struct qwe_proc p;
	sigset_t set, old;
	char buf[32] = "";
	pid_t orphan;
	int st, i;
	ssize_t n = 0;

	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &old);
	ASSERT_EQ(0, qwe_proc_become_subreaper());

	ASSERT_EQ(0, qwe_proc_spawn(&p, argv_orphan, NULL));
	for (i = 0; i < 300 && n <= 0; i++) {
		n = read(p.out_fd, buf, sizeof buf - 1);
		if (n <= 0)
			sleep_ms(10);
	}
	ASSERT(n > 0);
	orphan = (pid_t)atoi(buf);
	ASSERT(orphan > 0);
	ASSERT_EQ(p.pid, waitpid(p.pid, &st, 0));
	ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0);

	/* The leader is gone and reaped. The orphan is alive, still in the group,
	 * and now our child (not init's), so the group is not empty. */
	ASSERT_EQ(getpid(), getppid_of(orphan));
	ASSERT(!qwe_proc_group_empty(p.pid));

	/* Once it is killed, waiting reaps it and the group is empty. */
	ASSERT_EQ(0, kill(orphan, SIGKILL));
	for (i = 0; i < 300 && !qwe_proc_group_empty(p.pid); i++)
		sleep_ms(10);
	ASSERT(qwe_proc_group_empty(p.pid));
	/* It was reaped by us: there is nothing left to wait for. */
	ASSERT_EQ(-1, waitpid(orphan, &st, WNOHANG));

	close(p.out_fd);
	sigprocmask(SIG_SETMASK, &old, NULL);
	PASS();
}

/* A spawn that cannot get its pipes says which pipe call failed, with errno,
 * and leaks no descriptor. The limit is set to leave room for 0, 1 or 2 new fds. */
static int spawn_with_fd_room(int room, struct qwe_proc *p, int *open_before, int *open_after)
{
	struct rlimit lim, old;
	int first = dup(0), rc, again;

	close(first);
	getrlimit(RLIMIT_NOFILE, &old);
	lim = old;
	lim.rlim_cur = (rlim_t)(first + room);
	setrlimit(RLIMIT_NOFILE, &lim);
	*open_before = dup(0);
	close(*open_before);
	rc = qwe_proc_spawn(p, argv_true, NULL);
	again = dup(0);
	close(again);
	*open_after = again;
	setrlimit(RLIMIT_NOFILE, &old);
	return rc;
}

TEST spawn_reports_a_pipe_failure(void)
{
	struct qwe_proc p;
	int before, after;

	/* room for one fd: the first pipe fails */
	ASSERT_EQ(-1, spawn_with_fd_room(1, &p, &before, &after));
	ASSERT_EQ(EMFILE, errno);
	ASSERT_STR_EQ("pipe", p.fail_op);
	ASSERT_EQ(before, after);
	/* room for two: the first pipe is made, the close-on-exec one is not */
	ASSERT_EQ(-1, spawn_with_fd_room(2, &p, &before, &after));
	ASSERT_EQ(EMFILE, errno);
	ASSERT_STR_EQ("pipe", p.fail_op);
	ASSERT_EQ(before, after);
	PASS();
}

static char **argv_none(void *arg, int result_fd)
{
	(void)arg;
	(void)result_fd;
	return NULL;
}

static char **argv_missing(void *arg, int result_fd)
{
	static char *argv[] = {"/nonexistent/qwe-no-such-program", NULL};
	(void)arg;
	(void)result_fd;
	return argv;
}

/* The child that cannot build its argv exits 126; one that cannot exec, 127. */
TEST child_exit_codes_for_no_argv_and_no_program(void)
{
	struct qwe_proc p;
	int status;
	char buf[64];

	ASSERT_EQ(0, qwe_proc_spawn(&p, argv_none, NULL));
	while (read(p.out_fd, buf, sizeof buf) > 0)
		;
	ASSERT_EQ(p.pid, waitpid(p.pid, &status, 0));
	ASSERT(WIFEXITED(status));
	ASSERT_EQ(126, WEXITSTATUS(status));
	close(p.out_fd);
	close(p.res_fd);

	ASSERT_EQ(0, qwe_proc_spawn(&p, argv_missing, NULL));
	while (read(p.out_fd, buf, sizeof buf) > 0)
		;
	ASSERT_EQ(p.pid, waitpid(p.pid, &status, 0));
	ASSERT(WIFEXITED(status));
	ASSERT_EQ(127, WEXITSTATUS(status));
	close(p.out_fd);
	close(p.res_fd);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(child_is_group_leader);
	RUN_TEST(group_kill_no_orphans);
	RUN_TEST(subreaper_reaps_orphans);
	RUN_TEST(master_outside_step_groups);
	RUN_TEST(spawn_reports_a_pipe_failure);
	RUN_TEST(child_exit_codes_for_no_argv_and_no_program);
	GREATEST_MAIN_END();
}
