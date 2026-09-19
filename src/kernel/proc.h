/* Forks a step into its own process group with one merged output pipe. */
#ifndef QWE_KERNEL_PROC_H
#define QWE_KERNEL_PROC_H

#include <sys/types.h>

/* Runs in the forked child. Returns a NULL-terminated argv to exec, or NULL
 * to abort the step. The child has the parent's whole Lua state. */
typedef char **(*qwe_child_fn)(void *arg);

struct qwe_proc {
	pid_t pid;
	int out_fd; /* read end: child's stdout and stderr, merged in arrival order */
	const char *fail_op; /* after a failed spawn: "pipe" or "fork" */
};

/* SIGCHLD must be blocked by the caller (it is read through a signalfd), and
 * the child restores an empty mask. Returns 0, or -1 with errno set. */
int qwe_proc_spawn(struct qwe_proc *p, qwe_child_fn fn, void *arg);


/* Signals the step's whole process group, so the shell's pipeline and any
 * background children get it too. Returns 0, or -1 with errno set (ESRCH once
 * the group is gone). */
int qwe_proc_kill_group(const struct qwe_proc *p, int sig);

#endif
