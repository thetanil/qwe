/* The job state machine (design §7). A job only ever moves along these edges:
 *
 *   pending -> ready | skipped
 *   ready -> running | skipped
 *   running -> success | failed | terminating
 *   terminating -> cancelled
 *
 * success, failed, skipped and cancelled are final. */
#ifndef QWE_KERNEL_JOB_STATE_H
#define QWE_KERNEL_JOB_STATE_H

#include <stddef.h>

enum qwe_job_state {
	QWE_JOB_PENDING,
	QWE_JOB_READY,
	QWE_JOB_RUNNING,
	QWE_JOB_TERMINATING,
	QWE_JOB_SUCCESS,
	QWE_JOB_FAILED,
	QWE_JOB_SKIPPED,
	QWE_JOB_CANCELLED,
};

int qwe_job_transition_legal(enum qwe_job_state from, enum qwe_job_state to);

/* Moves *state to `to`. An illegal transition is a kernel bug: it prints
 * both states to stderr and aborts (in every build type, unlike assert). */
void qwe_job_transition(enum qwe_job_state *state, enum qwe_job_state to);

/* True for success, failed, skipped and cancelled. */
int qwe_job_state_is_final(enum qwe_job_state s);

/* The name used in result.json for a final state ("success", ...), or the
 * state's own name for a live one. */
const char *qwe_job_state_name(enum qwe_job_state s);


/* True if a finished run whose jobs ended in these (final) states counts as
 * a success: every job is success or skipped. A failed or cancelled job makes
 * it false. */
int qwe_jobs_all_ok(const enum qwe_job_state *states, size_t n);

#endif
