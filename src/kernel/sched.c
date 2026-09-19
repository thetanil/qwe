#include "src/kernel/sched.h"

/* How many jobs of the group hold a session: running, or granted earlier in this pass. */
static size_t group_used(const struct qwe_sched_job *jobs, size_t n, size_t group, const struct qwe_sched_event *out,
			 size_t granted)
{
	size_t i, used = 0;

	for (i = 0; i < n; i++)
		if (jobs[i].group == group && qwe_lc_state_is_running(*jobs[i].state))
			used++;
	for (i = 0; i < granted; i++)
		if (jobs[out[i].job].group == group)
			used++;
	return used;
}

size_t qwe_sched_pass(const struct qwe_sched_job *jobs, size_t n, long max_parallel, const long *caps,
		      struct qwe_sched_event *out)
{
	size_t i, k, running = 0, count = 0;


	for (i = 0; i < n; i++) {
		int all_final = 1, all_success = 1;

		if (*jobs[i].state != QWE_LC_PENDING)
			continue;
		for (k = 0; k < jobs[i].nneeds; k++) {
			enum qwe_lc_state s = *jobs[jobs[i].needs[k]].state;

			if (!qwe_lc_state_is_final(s))
				all_final = 0;
			else if (s != QWE_LC_SUCCESS)
				all_success = 0;
		}
		if (!all_final)
			continue;
		out[count].job = i;
		out[count].event = all_success ? QWE_LC_EV_NEEDS_MET : QWE_LC_EV_NEEDS_FAILED;
		count++;
	}
	if (count > 0)
		return count;

	for (i = 0; i < n; i++)
		running += qwe_lc_state_is_running(*jobs[i].state);
	for (i = 0; i < n; i++) {
		if (*jobs[i].state != QWE_LC_READY)
			continue;
		if (max_parallel > 0 && running + count >= (size_t)max_parallel)
			break;
		if (jobs[i].group && group_used(jobs, n, jobs[i].group, out, count) >= (size_t)caps[jobs[i].group - 1])
			continue;
		out[count].job = i;
		out[count].event = QWE_LC_EV_SLOT_GRANTED;
		count++;
	}
	return count;
}
