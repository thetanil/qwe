#define _GNU_SOURCE
#include "src/kernel/trace.h"

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
	clock_gettime(CLOCK_MONOTONIC, &t->t0);
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
	clock_gettime(CLOCK_MONOTONIC, &now);
	sec = (long)(now.tv_sec - t->t0.tv_sec);
	usec = (now.tv_nsec - t->t0.tv_nsec) / 1000;
	if (usec < 0) {
		sec--;
		usec += 1000000;
	}
	if (step >= 0)
		snprintf(stepbuf, sizeof stepbuf, "%ld", step);
	else
		strcpy(stepbuf, "-");
	n = snprintf(line, sizeof line, "%ld.%06ld %s %s %s %s %s %s %s%s%s\n", sec, usec, job ? job : "-", stepbuf,
		     qwe_lc_state_name(state), event, next ? next : "-", kind, reason ? reason : "-",
		     detail ? " " : "", detail ? detail : "");
	if (n > (int)sizeof line - 1)
		n = (int)sizeof line - 1;
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

const char *qwe_errno_name(int err)
{
	static char buf[24];
	const char *name = strerrorname_np(err);

	if (name)
		return name;
	snprintf(buf, sizeof buf, "E%d", err);
	return buf;
}
