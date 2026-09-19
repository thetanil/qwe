/* Which jobs may move on (design §7, §9). A pure decision over the job states
 * and the `needs:` graph: it runs nothing, owns no state, and changes no
 * job's state. It says which event each job should be sent. */
#ifndef QWE_KERNEL_SCHED_H
#define QWE_KERNEL_SCHED_H

#include "src/kernel/lifecycle.h"

#include <stddef.h>

struct qwe_sched_job {
	const enum qwe_lc_state *state; /* the job's live state; read only */
	const size_t *needs;            /* indexes into the same array */
	size_t nneeds;
};

struct qwe_sched_event {
	size_t job;
	enum qwe_lc_event event; /* needs-met, needs-failed or slot-granted */
};

/* One scheduling pass. If some pending job has all its needs final, the pass
 * is those jobs' needs-met (all needs success: the default join rule) or
 * needs-failed. Otherwise it is slot-granted for the ready jobs, lowest index
 * first, that fit under max_parallel (0 for unlimited) given the jobs already
 * running. Events are written to out[] (room for n) and their count returned.
 * The caller sends them, then passes again until a pass returns 0: a skip can
 * unlock the jobs that need the skipped one. */
size_t qwe_sched_pass(const struct qwe_sched_job *jobs, size_t n, long max_parallel, struct qwe_sched_event *out);

#endif
