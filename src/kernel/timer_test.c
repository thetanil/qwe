#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/timer.h"

#include <poll.h>
#include <time.h>

static void sleep_ms(long ms)
{
	struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
	nanosleep(&ts, NULL);
}

TEST timeout_and_grace_independent(void)
{
	struct qwe_timer timeout, grace;
	long before, after;

	ASSERT_EQ(0, qwe_timer_open(&timeout));
	ASSERT_EQ(0, qwe_timer_open(&grace));
	ASSERT(timeout.fd != grace.fd);

	/* Two different timerfds: arming the grace timer leaves the timeout unarmed. */
	ASSERT_EQ(0, qwe_timer_arm(&grace, 5000));
	ASSERT_EQ(-1L, qwe_timer_remaining_ms(&timeout));
	before = qwe_timer_remaining_ms(&grace);
	ASSERT(before > 3000 && before <= 5000);

	/* The timeout fires; the grace timer neither fires nor is re-armed. */
	ASSERT_EQ(0, qwe_timer_arm(&timeout, 50));
	sleep_ms(120);
	ASSERT(qwe_timer_expired(&timeout));
	ASSERT(!qwe_timer_expired(&grace));
	after = qwe_timer_remaining_ms(&grace);
	ASSERT(after > 0 && after < before); /* still counting down, from where it was */
	ASSERT(after >= before - 1000);      /* and not restarted at 5000 */

	/* Disarming one leaves the other alone. */
	ASSERT_EQ(0, qwe_timer_disarm(&timeout));
	ASSERT(qwe_timer_expired(&timeout)); /* fired stays fired */
	ASSERT(qwe_timer_remaining_ms(&grace) > 0);

	/* Now the grace timer fires; the timeout is not re-armed by it. */
	ASSERT_EQ(0, qwe_timer_arm(&grace, 30));
	sleep_ms(100);
	ASSERT(qwe_timer_expired(&grace));
	ASSERT_EQ(-1L, qwe_timer_remaining_ms(&timeout));

	qwe_timer_close(&timeout);
	qwe_timer_close(&grace);
	PASS();
}

TEST fd_becomes_readable(void)
{
	struct qwe_timer t;
	struct pollfd p;

	ASSERT_EQ(0, qwe_timer_open(&t));
	p.fd = t.fd;
	p.events = POLLIN;
	ASSERT_EQ(0, poll(&p, 1, 0)); /* not yet */
	ASSERT_EQ(0, qwe_timer_arm(&t, 30));
	ASSERT_EQ(1, poll(&p, 1, 1000));
	ASSERT(qwe_timer_expired(&t));
	qwe_timer_close(&t);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(timeout_and_grace_independent);
	RUN_TEST(fd_becomes_readable);
	GREATEST_MAIN_END();
}
