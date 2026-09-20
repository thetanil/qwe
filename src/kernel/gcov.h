/* Coverage of a process that ends in exec or _exit (ticket quality/09).
 *
 * gcov writes a process's counters when it exits normally. A step child (proc.c,
 * qwe.exec.run) ends in execvp or _exit, so everything it ran would be lost from
 * `bazel coverage`. Those places call qwe_gcov_dump() first. It is a no-op except
 * in `bazel coverage` builds, which .bazelrc gives -DQWE_GCOV. It resets the
 * counters after dumping them, so a process can dump more than once without
 * counting anything twice (gcov refuses a second dump until they are reset). */
#ifndef QWE_KERNEL_GCOV_H
#define QWE_KERNEL_GCOV_H

#ifdef QWE_GCOV
#include <stdlib.h>

extern void __gcov_dump(void);
extern void __gcov_reset(void);

/* Not when QWE_LUA_COVERAGE is unset: the OOM tests unset it, because the dump
 * allocates and would move the allocation counts they inject at. */
static inline void qwe_gcov_dump(void)
{
	if (!getenv("QWE_LUA_COVERAGE"))
		return;
	__gcov_dump();
	__gcov_reset();
}
#else
static inline void qwe_gcov_dump(void) {}
#endif

#endif
