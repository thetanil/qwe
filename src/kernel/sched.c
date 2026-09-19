#include "src/kernel/sched.h"

size_t qwe_sched_pass(struct qwe_sched_job *jobs, size_t n, long max_parallel, size_t *starts)
{
	size_t i, k, live = 0, count = 0;
	int changed;

	/* A skip can unlock the jobs that need the skipped one, so go to a fixpoint. */
	do {
		changed = 0;
		for (i = 0; i < n; i++) {
			int all_final = 1, all_success = 1;

			if (*jobs[i].state != QWE_JOB_PENDING)
				continue;
			for (k = 0; k < jobs[i].nneeds; k++) {
				enum qwe_job_state s = *jobs[jobs[i].needs[k]].state;

				if (!qwe_job_state_is_final(s))
					all_final = 0;
				else if (s != QWE_JOB_SUCCESS)
					all_success = 0;
			}
			if (!all_final)
				continue;
			qwe_job_transition(jobs[i].state, all_success ? QWE_JOB_READY : QWE_JOB_SKIPPED);
			changed = 1;
		}
	} while (changed);

	for (i = 0; i < n; i++)
		if (*jobs[i].state == QWE_JOB_RUNNING || *jobs[i].state == QWE_JOB_TERMINATING)
			live++;
	for (i = 0; i < n; i++) {
		if (*jobs[i].state != QWE_JOB_READY)
			continue;
		if (max_parallel > 0 && live + count >= (size_t)max_parallel)
			break;
		starts[count++] = i;
	}
	return count;
}
