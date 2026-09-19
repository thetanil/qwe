/* Forks a step into its own process group with one merged output pipe. */
#ifndef QWE_KERNEL_PROC_H
#define QWE_KERNEL_PROC_H

#include <sys/types.h>

/* Runs in the forked child. Returns a NULL-terminated argv to exec, or NULL
 * to abort the step. The child has the parent's whole Lua state. result_fd is
 * the write end of the step's result pipe (close-on-exec, so nothing the child
 * execs inherits it). */
typedef char **(*qwe_child_fn)(void *arg, int result_fd);

struct qwe_proc {
	pid_t pid;
	int out_fd; /* read end: child's stdout and stderr, merged in arrival order */
	int res_fd; /* read end of the result pipe, apart from stdout and stderr (non-blocking) */
	const char *fail_op; /* after a failed spawn: "pipe" or "fork" */
};

/* SIGCHLD must be blocked by the caller (it is read through a signalfd), and
 * the child restores an empty mask. Returns 0, or -1 with errno set. */
int qwe_proc_spawn(struct qwe_proc *p, qwe_child_fn fn, void *arg);


/* Signals the step's whole process group, so the shell's pipeline and any
 * background children get it too. Returns 0, or -1 with errno set (ESRCH once
 * the group is gone). */
int qwe_proc_kill_group(const struct qwe_proc *p, int sig);

/* Makes this process the parent of every orphan in its tree: a process whose
 * parent dies is reparented to qwe, not to init, so qwe can wait for it
 * (Linux PR_SET_CHILD_SUBREAPER; it is not inherited by children). Returns 0,
 * or -1 with errno set. */
int qwe_proc_become_subreaper(void);

/* True when no process of group pgid is left. It first reaps every process of
 * the group that has exited and is ours to reap; a zombie nobody has waited
 * for still counts as left. A process that has left the group (setsid) is not
 * seen: the known M1 gap (design §9.3). */
int qwe_proc_group_empty(pid_t pgid);

#endif
