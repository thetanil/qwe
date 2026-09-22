/* qwe run --summary: a markdown report of the run, appended to a file. */
#ifndef QWE_KERNEL_SUMMARY_H
#define QWE_KERNEL_SUMMARY_H

#include "src/kernel/result.h"

#include <stdio.h>

/* Appends one report to path: a level-3 heading (workflow_file, overall_outcome
 * -- "success", "failed" or "cancelled" -- and run_duration_ms), a jobs table,
 * and one steps table per job that started. A job with a failed or cancelled
 * step also gets a collapsed log-tail block, read from <run_dir>/<job-id>.log
 * (already redacted, since that is what the run wrote); run_dir may be NULL,
 * in which case no log tail is ever added. Returns 0, or -1 with errno set if
 * path cannot be opened or written; the run's own exit status is unaffected. */
int qwe_summary_write(const char *path, const char *run_dir, const char *workflow_file,
		      const char *overall_outcome, long run_duration_ms,
		      const struct qwe_job_result *jobs, size_t njobs);

#endif
