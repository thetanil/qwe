/* A one-shot timer on its own timerfd. Each timer is independent: arming,
 * firing or disarming one never touches another (design §9.2). */
#ifndef QWE_KERNEL_TIMER_H
#define QWE_KERNEL_TIMER_H

struct qwe_timer {
	int fd;    /* pollable; readable once the timer has fired */
	int fired; /* sticky: set the first time expiry is observed */
};

/* Returns 0, or -1 with errno set. The timer starts disarmed. */
int qwe_timer_open(struct qwe_timer *t);
void qwe_timer_close(struct qwe_timer *t);

/* Arms the timer to fire once, ms milliseconds from now. Re-arming a timer
 * that has not fired moves its deadline; a timer that has fired stays fired. */
int qwe_timer_arm(struct qwe_timer *t, long ms);
/* Returns 0, or -1 with errno set (the timer may then still fire). */
int qwe_timer_disarm(struct qwe_timer *t);

/* Non-blocking. Returns 1 once the timer has fired (and keeps returning 1). */
int qwe_timer_expired(struct qwe_timer *t);

/* Milliseconds until it fires, or -1 if it is disarmed or already fired. */
long qwe_timer_remaining_ms(const struct qwe_timer *t);

#endif
