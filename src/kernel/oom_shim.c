#include "src/kernel/oom_shim.h"
#include "src/kernel/gcov.h"

#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t a, size_t b);
void *__real_realloc(void *p, size_t n);
char *__real_strdup(const char *s);
char *__real_strndup(const char *s, size_t n);

static pid_t armed_pid; /* the process that called qwe_oom_arm */
static long fail_at, calls, fired;
static long child_fail_at;
static pid_t child_pid; /* the child whose count child_calls is */
static long child_calls;
static char child_log[512];

static unsigned oom_probe_timeout_seconds(void)
{
	const char *s = getenv("QWE_OOM_PROBE_TIMEOUT");
	char *end;
	long n;

	if (!s || !*s)
		return 30;
	errno = 0;
	n = strtol(s, &end, 10);
	if (errno != 0 || *end != 0 || n <= 0 || n > INT_MAX)
		return 30;
	return (unsigned)n;
}

void qwe_oom_arm(long n)
{
	armed_pid = getpid();
	fail_at = n;
	calls = 0;
	fired = 0;
}

void qwe_oom_arm_child(long n)
{
	if (!armed_pid)
		armed_pid = getpid();
	child_fail_at = n;
}

void qwe_oom_child_log(const char *path)
{
	if (!path)
		child_log[0] = 0;
	else
		strncpy(child_log, path, sizeof child_log - 1);
}

long qwe_oom_count(void)
{
	return calls;
}

int qwe_oom_fired(void)
{
	return fired != 0;
}



extern char __executable_start;

/* With QWE_OOM_SITE_LOG=<file>, appends the address of the failed call, as an
 * offset into the executable, one hex number per line. addr2line -e <test>
 * turns it into the call site, which is how to check that a test reaches a
 * given allocation. Nothing here allocates. */
static void log_site(void *ra)
{
	const char *path = getenv("QWE_OOM_SITE_LOG");
	char line[24];
	unsigned long off = (unsigned long)((char *)ra - &__executable_start);
	int i = sizeof line - 1, fd;

	if (!path)
		return;
	line[i] = 10;
	do {
		line[--i] = "0123456789abcdef"[off & 15];
		off >>= 4;
	} while (off);
	fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
	if (fd >= 0) {
		(void)!write(fd, line + i, sizeof line - (size_t)i);
		close(fd);
	}
}

static int should_fail(void *ra)
{
	pid_t me;

	if (!fail_at && !child_fail_at)
		return 0;
	me = getpid();
	if (armed_pid == 0 || me == armed_pid) {
		if (fail_at && ++calls == fail_at) {
			fired = 1;
			log_site(ra);
			return 1;
		}
		return 0;
	}
	if (child_pid != me) {
		child_pid = me;
		child_calls = 0;
	}
	if (child_fail_at && ++child_calls == child_fail_at) {
		log_site(ra);
		if (child_log[0]) {
			int fd = open(child_log, O_WRONLY | O_APPEND | O_CREAT, 0600);

			if (fd >= 0) {
				(void)!write(fd, "x\n", 2);
				close(fd);
			}
		}
		return 1;
	}
	return 0;
}

void *__wrap_malloc(size_t n) { return should_fail(__builtin_return_address(0)) ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { return should_fail(__builtin_return_address(0)) ? NULL : __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { return should_fail(__builtin_return_address(0)) ? NULL : __real_realloc(p, n); }
char *__wrap_strdup(const char *s) { return should_fail(__builtin_return_address(0)) ? NULL : __real_strdup(s); }
char *__wrap_strndup(const char *s, size_t n) { return should_fail(__builtin_return_address(0)) ? NULL : __real_strndup(s, n); }

#include <signal.h>
#include <sys/wait.h>

int qwe_oom_probe(long n, long child_n, int (*fn)(void *), void *arg, struct qwe_oom_outcome *out)
{
	int errp[2], cntp[2], status;
	ssize_t got;
	size_t have = 0;
	long report[2];
	pid_t pid;

	memset(out, 0, sizeof *out);
	if (pipe(errp) < 0 || pipe(cntp) < 0)
		return -1;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		int nul = open("/dev/null", O_RDONLY), rc;

		dup2(errp[1], 2);
		dup2(errp[1], 1);
		if (nul >= 0) {
			dup2(nul, 0);
			close(nul);
		}
		close(errp[0]);
		close(errp[1]);
		close(cntp[0]);
		alarm(oom_probe_timeout_seconds());
		qwe_oom_arm(n);
		if (child_n)
			qwe_oom_arm_child(child_n);
		rc = fn(arg);
		report[0] = qwe_oom_count();
		report[1] = qwe_oom_fired();
		(void)!write(cntp[1], report, sizeof report);
		/* disarmed first: the dump allocates, and must not be the one that fails */
		fail_at = 0;
		child_fail_at = 0;
		qwe_gcov_dump();
		_exit(rc);
	}
	close(errp[1]);
	close(cntp[1]);
	while (have < sizeof out->err - 1 && (got = read(errp[0], out->err + have, sizeof out->err - 1 - have)) > 0)
		have += (size_t)got;
	out->err[have] = 0;
	while (read(errp[0], report, sizeof report) > 0)
		; /* drain, so a chatty child is not blocked on a full pipe */
	close(errp[0]);
	if (read(cntp[0], report, sizeof report) == (ssize_t)sizeof report) {
		out->count = report[0];
		out->fired = (int)report[1];
	}
	close(cntp[0]);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		out->exited = 1;
		out->code = WEXITSTATUS(status);
	} else {
		out->signal = WTERMSIG(status);
	}
	return 0;
}
