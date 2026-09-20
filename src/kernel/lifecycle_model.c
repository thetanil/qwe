#include "src/kernel/lifecycle_model.h"

#define EV(e) (1u << QWE_LC_EV_##e)

/* Facts about a state, each one a reason an event can or cannot occur. */

/* A live job has its timeout armed from the moment it starts. */
static int job_timer_armed(enum qwe_lc_state s)
{
	return s >= QWE_LC_BETWEEN_STEPS && s <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL;
}

/* A step has been spawned and has not yet ended: its leader may be alive. */
static int step_in_flight(enum qwe_lc_state s)
{
	return s >= QWE_LC_STEP_RUNNING && s <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL;
}

/* The step timer is one-shot and runs until the step ends. */
static int step_timer_may_fire(enum qwe_lc_state s)
{
	switch (s) {
	case QWE_LC_STEP_STOPPING:
	case QWE_LC_STEP_STOPPING_COE:
	case QWE_LC_STEP_KILLING:
	case QWE_LC_STEP_KILLING_COE:
		return 0; /* it fired: that is how these states began */
	default:
		return step_in_flight(s);
	}
}

/* The job timer is one-shot too: once it has fired, the job is cancelling. */
static int job_timer_may_fire(enum qwe_lc_state s)
{
	switch (s) {
	case QWE_LC_SETTLING_CANCEL_TIMEOUT_TERM:
	case QWE_LC_SETTLING_CANCEL_TIMEOUT_KILL:
		return 0;
	default:
		return job_timer_armed(s);
	}
}

/* The grace timer runs from the first SIGTERM until it fires once. */
static int grace_timer_running(enum qwe_lc_state s)
{
	switch (s) {
	case QWE_LC_STEP_STOPPING:
	case QWE_LC_STEP_STOPPING_COE:
	case QWE_LC_SETTLING_CONTINUE_TERM:
	case QWE_LC_SETTLING_FAIL_TERM:
	case QWE_LC_SETTLING_CANCEL_TIMEOUT_TERM:
	case QWE_LC_SETTLING_CANCEL_REQUESTED_TERM:
		return 1;
	default:
		return 0;
	}
}

/* The process group can be empty only once the leader is gone, and the shell
 * has seen that: the settling states. */
static int waiting_for_empty_group(enum qwe_lc_state s)
{
	return s >= QWE_LC_SETTLING_CONTINUE_TERM && s <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL;
}

unsigned qwe_lcm_events(enum qwe_lc_state s)
{
	unsigned ev = 0;

	switch (s) {
	case QWE_LC_PENDING:
		return EV(NEEDS_MET) | EV(NEEDS_FAILED) | EV(CANCEL) | EV(SKIP);
	case QWE_LC_READY:
		return EV(SLOT_GRANTED) | EV(CANCEL) | EV(SKIP);
	case QWE_LC_BETWEEN_STEPS:
		/* the shell holds the cursor: it says what comes next, or that
		 * the job could not be started */
		ev = EV(NEXT_STEP) | EV(NO_MORE_STEPS) | EV(START_FAILED);
		break;
	case QWE_LC_STEP_RUNNING:
	case QWE_LC_STEP_RUNNING_COE:
		/* the state is entered before the spawn action runs, so the
		 * spawn can fail here */
		ev = EV(START_FAILED);
		break;
	case QWE_LC_SUCCESS:
	case QWE_LC_FAILED:
	case QWE_LC_SKIPPED:
	case QWE_LC_CANCELLED:
		return EV(CANCEL); /* the cancel is broadcast to every job */
	default:
		break;
	}
	if (step_in_flight(s))
		ev |= EV(LEADER_EXIT_OK) | EV(LEADER_EXIT_FAIL);
	if (step_timer_may_fire(s))
		ev |= EV(STEP_TIMEOUT);
	if (job_timer_may_fire(s))
		ev |= EV(JOB_TIMEOUT);
	if (grace_timer_running(s))
		ev |= EV(GRACE_EXPIRED);
	if (waiting_for_empty_group(s))
		ev |= EV(GROUP_EMPTY);
	if (job_timer_armed(s))
		ev |= EV(CANCEL);
	return ev;
}
