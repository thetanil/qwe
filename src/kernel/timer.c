#define _POSIX_C_SOURCE 200809L
#include "src/kernel/timer.h"

#include <errno.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

int qwe_timer_open(struct qwe_timer *t)
{
	t->fired = 0;
	t->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	return t->fd < 0 ? -1 : 0;
}

void qwe_timer_close(struct qwe_timer *t)
{
	if (t->fd >= 0)
		close(t->fd);
	t->fd = -1;
}

int qwe_timer_arm(struct qwe_timer *t, long ms)
{
	struct itimerspec its = {{0, 0}, {ms / 1000, (ms % 1000) * 1000000L}};

	if (ms <= 0)
		its.it_value.tv_nsec = 1; /* zero would disarm; fire at once instead */
	return timerfd_settime(t->fd, 0, &its, NULL);
}

int qwe_timer_disarm(struct qwe_timer *t)
{
	struct itimerspec zero = {{0, 0}, {0, 0}};

	return timerfd_settime(t->fd, 0, &zero, NULL);
}

int qwe_timer_expired(struct qwe_timer *t)
{
	uint64_t count;

	if (!t->fired && read(t->fd, &count, sizeof count) == (ssize_t)sizeof count)
		t->fired = 1;
	return t->fired;
}

long qwe_timer_remaining_ms(const struct qwe_timer *t)
{
	struct itimerspec its;

	if (t->fired || timerfd_gettime(t->fd, &its) < 0)
		return -1;
	if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0)
		return -1;
	return its.it_value.tv_sec * 1000L + its.it_value.tv_nsec / 1000000L;
}
