/* Structural checks on the jobs' `needs:` graph. */
#ifndef QWE_KERNEL_DAG_H
#define QWE_KERNEL_DAG_H

#include <stddef.h>

struct qwe_dag_job {
	const char *id;
	const char *const *needs;
	size_t nneeds;
};

enum qwe_dag_status {
	QWE_DAG_OK,
	QWE_DAG_UNKNOWN_NEED, /* job `job`'s need number `need` names no job */
	QWE_DAG_CYCLE,        /* `job` is the first job of a cycle */
};

struct qwe_dag_error {
	enum qwe_dag_status status;
	size_t job;
	size_t need;
	char message[256];
};

/* Checks unknown needs first, then cycles, and reports the first problem it
 * finds. The message names the jobs involved (a cycle as "a -> b -> a").
 * Returns the status; err is filled unless it is NULL. */
enum qwe_dag_status qwe_dag_check(const struct qwe_dag_job *jobs, size_t n, struct qwe_dag_error *err);

#endif
