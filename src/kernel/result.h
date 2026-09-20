/* result.json: every job's and step's outcome, reason and times. */
#ifndef QWE_KERNEL_RESULT_H
#define QWE_KERNEL_RESULT_H

#include <stdio.h>
#include <time.h>

struct qwe_step_result {
	const char *id; /* may be NULL */
	const char *outcome;
	const char *reason; /* may be NULL */
	int changed;
	char *outputs_json; /* the step's outputs as a JSON object; NULL if it has none */
	time_t started, ended; /* 0 if it never started */
};

struct qwe_job_result {
	const char *id;
	const char *outcome;
	const char *reason; /* may be NULL */
	const char *detail; /* what the reason came with (a disabled target's note), or NULL: then not written */
	time_t started, ended; /* 0 if it never started */
	unsigned long dropped_bytes;
	const struct qwe_step_result *steps;
	size_t nsteps;
};

/* Writes {"run_id":..., "jobs": {...}} to fp. Returns 0, or -1 on a write error. */
int qwe_result_write(FILE *fp, const char *run_id, const struct qwe_job_result *jobs, size_t njobs);

/* Writes s as a JSON string literal, quoted and escaped. */
void qwe_json_string(FILE *fp, const char *s);

#endif
