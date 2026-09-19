/* The lifecycle trace: one line per record, written unbuffered to
 * <run-dir>/lifecycle.trace, so nothing is lost if qwe aborts.
 *
 *   <seconds since the run started> <job> <step> <state> <event> <next state>
 *   <kind> <reason>[ <detail>]
 *
 * step is the step's index or "-", reason is "-" for none. kind is one of
 * transition, ignore, impossible, stale, or event (--debug only: the event as
 * it arrived, before the table). The optional detail is "op=<name>
 * errno=<NAME>" for a step or job that could not be started. */
#ifndef QWE_KERNEL_TRACE_H
#define QWE_KERNEL_TRACE_H

#include "src/kernel/lifecycle.h"

#include <time.h>

struct qwe_trace {
	int fd; /* -1 when closed */
	int debug;
	struct timespec t0;
	/* who the next lookup is about, for the abort hook */
	const char *job;
	long step;
};

/* Opens (truncating) the file. Returns 0, or -1 with errno set. */
int qwe_trace_open(struct qwe_trace *t, const char *path, int debug);
void qwe_trace_close(struct qwe_trace *t);

/* Writes one record. step < 0 prints "-"; next and reason and detail may be NULL. */
void qwe_trace_record(struct qwe_trace *t, const char *job, long step, enum qwe_lc_state state,
		      const char *event, const char *next, const char *kind, const char *reason,
		      const char *detail);

/* Makes an impossible lookup write its line (job and step from t->job and
 * t->step) before it aborts. */
void qwe_trace_install_abort_hook(struct qwe_trace *t);

/* "EAGAIN" and the like, or "E<number>". Never NULL. */
const char *qwe_errno_name(int err);

#endif
