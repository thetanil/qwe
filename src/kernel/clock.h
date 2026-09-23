/* The monotonic clock every timing in the kernel reads. */
#ifndef QWE_KERNEL_CLOCK_H
#define QWE_KERNEL_CLOCK_H

#include <stdlib.h>
#include <time.h>

/* CLOCK_MONOTONIC now. On Linux clock_gettime fails only for an unknown clock
 * (EINVAL) or a bad pointer (EFAULT), neither of which this call can pass, so
 * a failure is a broken kernel: abort rather than time a step from garbage. */
static inline struct timespec qwe_mono_now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		abort();
	return ts;
}

#endif
