/* qwe run --summary: a markdown report of the run, appended to a file. */
#ifndef QWE_KERNEL_SUMMARY_H
#define QWE_KERNEL_SUMMARY_H

#include "src/kernel/result.h"

#include <stdio.h>

/* Appends one report to path: a level-3 heading (workflow_file, overall_outcome
 * -- "success", "failed" or "cancelled" -- and run_duration_ms), a jobs table,
 * and one steps table per job that started. Returns 0, or -1 with errno set if
 * path cannot be opened or written; the run's own exit status is unaffected. */
int qwe_summary_write(const char *path, const char *workflow_file, const char *overall_outcome,
		      long run_duration_ms, const struct qwe_job_result *jobs, size_t njobs);

#endif
