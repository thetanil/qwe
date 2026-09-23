#define _GNU_SOURCE
#include "src/kernel/trace.h"

#include "src/kernel/clock.h"
#include "src/kernel/fmt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int qwe_trace_open(struct qwe_trace *t, const char *path, int debug)
{
	memset(t, 0, sizeof *t);
	t->step = -1;
	t->debug = debug;
	t->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (t->fd < 0)
		return -1;
	t->t0 = qwe_mono_now();
	return 0;
}

void qwe_trace_close(struct qwe_trace *t)
{
	if (t->fd >= 0)
		close(t->fd);
	t->fd = -1;
}

void qwe_trace_record(struct qwe_trace *t, const char *job, long step, enum qwe_lc_state state,
		      const char *event, const char *next, const char *kind, const char *reason,
		      const char *detail)
{
	char line[512], stepbuf[24];
	struct timespec now;
	long sec, usec;
	int n;

	if (t->fd < 0)
		return;
	now = qwe_mono_now();
	sec = (long)(now.tv_sec - t->t0.tv_sec);
	usec = (now.tv_nsec - t->t0.tv_nsec) / 1000;
	if (usec < 0) {
		sec--;
		usec += 1000000;
	}
	if (step >= 0)
		qwe_xfmt(stepbuf, sizeof stepbuf, "%ld", step);
	else
		memcpy(stepbuf, "-", sizeof "-");
	n = snprintf(line, sizeof line, "%ld.%06ld %s %s %s %s %s %s %s%s%s\n", sec, usec, job ? job : "-", stepbuf,
		     qwe_lc_state_name(state), event, next ? next : "-", kind, reason ? reason : "-",
		     detail ? " " : "", detail ? detail : "");
	if (n > (int)sizeof line - 1) { /* cut, but keep the record one line */
		n = (int)sizeof line - 1;
		line[n - 1] = '\n';
	}
	/* One write per line: the file is never behind, so an abort loses nothing. */
	if (write(t->fd, line, (size_t)n) < 0)
		return;
}

static void abort_hook(enum qwe_lc_state s, enum qwe_lc_event e, void *arg)
{
	struct qwe_trace *t = arg;

	qwe_trace_record(t, t->job, t->step, s, qwe_lc_event_name(e), NULL, "impossible", NULL, NULL);
}

void qwe_trace_install_abort_hook(struct qwe_trace *t)
{
	qwe_lc_set_abort_hook(abort_hook, t);
}

/* strerrorname_np is a GNU extension from glibc 2.32. Without it (musl, older
 * glibc) the names of the errnos a step can fail to start with come from a
 * table; any other errno is "E<number>", as it always was for an unknown one. */
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 32) && !defined(QWE_NO_STRERRORNAME_NP)
#define HAVE_STRERRORNAME_NP 1
#endif
#endif

#ifndef HAVE_STRERRORNAME_NP
#define ERRNO_NAME(e) {e, #e}
static const struct {
	int err;
	const char *name;
} errno_names[] = {
	ERRNO_NAME(EPERM),  ERRNO_NAME(ENOENT), ERRNO_NAME(ESRCH),  ERRNO_NAME(EINTR),
	ERRNO_NAME(EIO),    ERRNO_NAME(E2BIG),  ERRNO_NAME(ENOEXEC), ERRNO_NAME(EBADF),
	ERRNO_NAME(ECHILD), ERRNO_NAME(EAGAIN), ERRNO_NAME(ENOMEM), ERRNO_NAME(EACCES),
	ERRNO_NAME(EFAULT), ERRNO_NAME(EBUSY),  ERRNO_NAME(EEXIST), ERRNO_NAME(ENOTDIR),
	ERRNO_NAME(EISDIR), ERRNO_NAME(EINVAL), ERRNO_NAME(ENFILE), ERRNO_NAME(EMFILE),
	ERRNO_NAME(ENOSPC), ERRNO_NAME(EPIPE),  ERRNO_NAME(ENAMETOOLONG),
};

static const char *errno_name_np(int err)
{
	size_t i;

	for (i = 0; i < sizeof errno_names / sizeof *errno_names; i++)
		if (errno_names[i].err == err)
			return errno_names[i].name;
	return NULL;
}
#else
#define errno_name_np strerrorname_np
#endif

const char *qwe_errno_name(int err)
{
	static char buf[24];
	const char *name = errno_name_np(err);

	if (name)
		return name;
	qwe_xfmt(buf, sizeof buf, "E%d", err);
	return buf;
}
