#include "src/kernel/sched.h"

size_t qwe_sched_pass(const struct qwe_sched_job *jobs, size_t n, long max_parallel, struct qwe_sched_event *out)
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
		out[count].job = i;
		out[count].event = QWE_LC_EV_SLOT_GRANTED;
		count++;
	}
	return count;
}
