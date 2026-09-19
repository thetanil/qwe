/* The kernel's one public header. Every subcommand reaches the kernel here. */
#ifndef QWE_KERNEL_QWE_H
#define QWE_KERNEL_QWE_H

#define QWE_VERSION "0.1.0"

/* Exit codes (spec, "Exit codes"). */
#define QWE_EXIT_OK 0
#define QWE_EXIT_FAILED 1
#define QWE_EXIT_USAGE 2
#define QWE_EXIT_CANCELLED 130

/* Returns "qwe <version>". */
const char *qwe_version_string(void);

struct qwe_run_options {
	int debug; /* --debug: the lifecycle trace also records every event */
};

/* Runs the workflow at path to completion; returns the process exit code. */
int qwe_run_workflow(const char *path, const struct qwe_run_options *opts);

/* Validates the workflow at path, printing errors as file:line:col. Returns the exit code. */
int qwe_validate_workflow(const char *path);

/* Prints "qwe <cmd>: not implemented" to stderr; returns QWE_EXIT_USAGE. */
int qwe_not_implemented(const char *cmd);

#endif
