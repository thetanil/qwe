/* A failing allocator for tests: fails the nth allocation and is otherwise
 * transparent. Test-only: the library is testonly, so no shipping target can
 * depend on it, and it takes effect only in a test linked with the --wrap
 * flags this library carries (linkstatic: --wrap reaches only objects linked
 * into the binary).
 *
 * Two independent counters, because a fork copies the counter and both sides
 * would otherwise fail the same allocation number:
 *
 *   the process that armed it   counts its own allocations from the arm;
 *   any other process (a fork   counts from zero at its first allocation, so
 *   of it: a step child)        "the child's nth" means the same every time.
 *
 * A child that fires appends a line to the file named by qwe_oom_child_log,
 * since its memory is gone by the time the parent looks. Nothing here
 * allocates.
 *
 * With QWE_OOM_SITE_LOG=<file> in the environment, the failing call is also
 * appended to that file as an offset into the executable (hex, one per line);
 * addr2line -i -e <test binary> turns the offsets into call sites. That is how
 * to check which allocations a test really fails. */
#ifndef QWE_KERNEL_OOM_SHIM_H
#define QWE_KERNEL_OOM_SHIM_H

/* The nth allocation in this process from now fails; 0 disarms. Resets the counters. */
void qwe_oom_arm(long n);

/* The nth allocation in each process forked from this one fails; 0 disarms. */
void qwe_oom_arm_child(long n);

/* Where a forked process records that it failed an allocation (NULL: nowhere). */
void qwe_oom_child_log(const char *path);

/* Allocations this process has made since it was armed, and whether its nth failed. */
long qwe_oom_count(void);
int qwe_oom_fired(void);

/* One injected run, in a process of its own so that a fault, an abort or a hang
 * is an outcome to report rather than the end of the test. The child arms the
 * shim (n for itself, child_n for the processes it forks; 0 = not), calls fn,
 * and exits with its return value. It gets 30 s. */
struct qwe_oom_outcome {
	int exited; /* it exited by itself... */
	int code; /* ...with this code */
	int signal; /* or it was killed by this signal (SIGALRM: a hang) */
	long count; /* allocations the child made, and */
	int fired; /* whether its nth failed */
	char err[8192]; /* what it wrote to stdout and stderr, merged */
};

int qwe_oom_probe(long n, long child_n, int (*fn)(void *), void *arg, struct qwe_oom_outcome *out);

#endif
