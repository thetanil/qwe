#include "src/kernel/job_state.h"

#include <stdio.h>
#include <stdlib.h>


int qwe_job_transition_legal(enum qwe_job_state from, enum qwe_job_state to)
{
	switch (from) {
	case QWE_JOB_PENDING:
		return to == QWE_JOB_READY || to == QWE_JOB_SKIPPED;
	case QWE_JOB_READY:
		return to == QWE_JOB_RUNNING || to == QWE_JOB_SKIPPED;
	case QWE_JOB_RUNNING:
		return to == QWE_JOB_SUCCESS || to == QWE_JOB_FAILED || to == QWE_JOB_TERMINATING;
	case QWE_JOB_TERMINATING:
		return to == QWE_JOB_CANCELLED;
	default:
		return 0; /* final states go nowhere */
	}
}

const char *qwe_job_state_name(enum qwe_job_state s)
{
	switch (s) {
	case QWE_JOB_PENDING:
		return "pending";
	case QWE_JOB_READY:
		return "ready";
	case QWE_JOB_RUNNING:
		return "running";
	case QWE_JOB_TERMINATING:
		return "terminating";
	case QWE_JOB_SUCCESS:
		return "success";
	case QWE_JOB_FAILED:
		return "failed";
	case QWE_JOB_SKIPPED:
		return "skipped";
	case QWE_JOB_CANCELLED:
		return "cancelled";
	}
	return "?";
}

int qwe_job_state_is_final(enum qwe_job_state s)
{
	return s == QWE_JOB_SUCCESS || s == QWE_JOB_FAILED || s == QWE_JOB_SKIPPED || s == QWE_JOB_CANCELLED;
}

void qwe_job_transition(enum qwe_job_state *state, enum qwe_job_state to)
{
	if (!qwe_job_transition_legal(*state, to)) {
		fprintf(stderr, "qwe: internal error: illegal job transition %s -> %s\n",
			qwe_job_state_name(*state), qwe_job_state_name(to));
		abort();
	}
	*state = to;
}

int qwe_jobs_all_ok(const enum qwe_job_state *states, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (states[i] != QWE_JOB_SUCCESS && states[i] != QWE_JOB_SKIPPED)
			return 0;
	return 1;
}
