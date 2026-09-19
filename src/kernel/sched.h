/* Which jobs may start (design §7, §9). A pure decision over the job states
 * and the `needs:` graph: it runs nothing and owns no state. */
#ifndef QWE_KERNEL_SCHED_H
#define QWE_KERNEL_SCHED_H

#include "src/kernel/job_state.h"

#include <stddef.h>

struct qwe_sched_job {
	enum qwe_job_state *state; /* the job's live state; the scheduler moves it */
	const size_t *needs;       /* indexes into the same array */
	size_t nneeds;
};

/* One scheduling pass. Every pending job whose needs are all final moves to
 * skipped (some need is not success: the default join rule) or ready. Then
 * the ready jobs, lowest index first, that fit under max_parallel (0 for
 * unlimited) given the jobs already running or terminating are written to
 * starts[] (room for n). The caller moves each of those to running.
 * Returns how many were written. */
size_t qwe_sched_pass(struct qwe_sched_job *jobs, size_t n, long max_parallel, size_t *starts);

#endif
