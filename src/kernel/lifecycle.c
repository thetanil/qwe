#include "src/kernel/lifecycle.h"

#include <stdio.h>
#include <stdlib.h>

/* The table. One row per state, one column per event, in the enum's order:
 *
 *   needs-met needs-failed slot-granted next-step no-more-steps start-failed
 *   leader-exit-ok leader-exit-fail step-timeout job-timeout cancel
 *   grace-expired group-empty
 *
 * Every cell is written out: T is a transition, I an ignore (with its
 * justification), X an impossible cell. A cell left out would be QWE_LC_UNSET,
 * which the tests refuse.
 *
 * Precedence when triggers meet: cancel-requested > job timeout > step
 * failure > continue. It shows in the settling rows: a later, stronger trigger
 * changes the outcome but never re-sends a signal or re-arms the grace timer. */

#define NO QWE_LC_ACT_NONE
#define START QWE_LC_ACT_START_JOB
#define SPAWN QWE_LC_ACT_SPAWN_STEP
#define TERM QWE_LC_ACT_SIGTERM_GROUP
#define GRACE QWE_LC_ACT_ARM_GRACE
#define KILL QWE_LC_ACT_SIGKILL_GROUP
#define REC_STEP QWE_LC_ACT_RECORD_STEP
#define REC_JOB QWE_LC_ACT_RECORD_JOB
#define END QWE_LC_ACT_END_JOB

#define R_NONE QWE_LC_REASON_NONE
#define R_DEP QWE_LC_REASON_DEPENDENCY_FAILED
#define R_CANCEL QWE_LC_REASON_CANCEL_REQUESTED
#define R_TIMEOUT QWE_LC_REASON_TIMEOUT
#define R_ENGINE QWE_LC_REASON_ENGINE_ERROR
#define R_EVENT QWE_LC_REASON_FROM_EVENT
#define R_CARRIED QWE_LC_REASON_CARRIED

#define S(n) QWE_LC_##n

#define T(next, reason, ...) {QWE_LC_TRANSITION, S(next), reason, 0, {__VA_ARGS__}, NULL}
/* like T, but the next-step event says which of two states */
#define TC(next, reason, ...) {QWE_LC_TRANSITION, S(next), reason, 1, {__VA_ARGS__}, NULL}
#define I(state, why) {QWE_LC_IGNORE, S(state), R_NONE, 0, {NO}, why}
#define X {QWE_LC_IMPOSSIBLE, 0, R_NONE, 0, {NO}, NULL}

#define STALE_LEADER "the outcome is already decided; the group emptying ends the step"
#define STALE_STEP_TIMER "the outcome is already decided; the step timer may still fire until the group is empty"
#define FINISHED "the operator's cancel is sent to every job; a finished job has nothing to cancel"

static const struct qwe_lc_cell table[QWE_LC_NSTATES][QWE_LC_NEVENTS] = {
	/* pending: waiting for its needs */
	[S(PENDING)] = {
		T(READY, R_NONE, NO), T(SKIPPED, R_DEP, REC_JOB, END), X, X, X, X, X, X, X, X,
		T(SKIPPED, R_CANCEL, REC_JOB, END), X, X},
	/* ready: waiting for a max-parallel slot */
	[S(READY)] = {
		X, X, T(BETWEEN_STEPS, R_NONE, START), X, X, X, X, X, X, X,
		T(SKIPPED, R_CANCEL, REC_JOB, END), X, X},
	/* between-steps: the job is started and no step is live */
	[S(BETWEEN_STEPS)] = {
		X, X, X, TC(STEP_RUNNING, R_NONE, SPAWN), T(SUCCESS, R_NONE, REC_JOB, END),
		T(FAILED, R_ENGINE, REC_JOB, END), X, X, X,
		T(CANCELLED, R_TIMEOUT, REC_JOB, END), T(CANCELLED, R_CANCEL, REC_JOB, END), X, X},
	/* step-running: the leader is alive, nothing is being torn down. A failed
	 * spawn is start-failed here: the step's state is entered before the
	 * spawn action runs. continue-on-error does not rescue it. */
	[S(STEP_RUNNING)] = {
		X, X, X, X, X, T(FAILED, R_ENGINE, REC_STEP, REC_JOB, END),
		T(SETTLING_CONTINUE_TERM, R_NONE, TERM, GRACE, REC_STEP),
		T(SETTLING_FAIL_TERM, R_EVENT, TERM, GRACE, REC_STEP),
		T(STEP_STOPPING, R_TIMEOUT, TERM, GRACE),
		T(SETTLING_CANCEL_TIMEOUT_TERM, R_TIMEOUT, TERM, GRACE, REC_STEP),
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, TERM, GRACE, REC_STEP), X, X},
	[S(STEP_RUNNING_COE)] = {
		X, X, X, X, X, T(FAILED, R_ENGINE, REC_STEP, REC_JOB, END),
		T(SETTLING_CONTINUE_TERM, R_NONE, TERM, GRACE, REC_STEP),
		T(SETTLING_CONTINUE_TERM, R_EVENT, TERM, GRACE, REC_STEP),
		T(STEP_STOPPING_COE, R_TIMEOUT, TERM, GRACE),
		T(SETTLING_CANCEL_TIMEOUT_TERM, R_TIMEOUT, TERM, GRACE, REC_STEP),
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, TERM, GRACE, REC_STEP), X, X},
	/* step-stopping: the step timed out; TERM is sent and grace is running. A
	 * timed-out step failed whatever its leader's status was. */
	[S(STEP_STOPPING)] = {
		X, X, X, X, X, X,
		T(SETTLING_FAIL_TERM, R_TIMEOUT, REC_STEP),
		T(SETTLING_FAIL_TERM, R_TIMEOUT, REC_STEP), X,
		T(SETTLING_CANCEL_TIMEOUT_TERM, R_TIMEOUT, REC_STEP),
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, REC_STEP),
		T(STEP_KILLING, R_NONE, KILL), X},
	[S(STEP_STOPPING_COE)] = {
		X, X, X, X, X, X,
		T(SETTLING_CONTINUE_TERM, R_TIMEOUT, REC_STEP),
		T(SETTLING_CONTINUE_TERM, R_TIMEOUT, REC_STEP), X,
		T(SETTLING_CANCEL_TIMEOUT_TERM, R_TIMEOUT, REC_STEP),
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, REC_STEP),
		T(STEP_KILLING_COE, R_NONE, KILL), X},
	/* step-killing: grace expired; KILL is sent. */
	[S(STEP_KILLING)] = {
		X, X, X, X, X, X,
		T(SETTLING_FAIL_KILL, R_TIMEOUT, REC_STEP),
		T(SETTLING_FAIL_KILL, R_TIMEOUT, REC_STEP), X,
		T(SETTLING_CANCEL_TIMEOUT_KILL, R_TIMEOUT, REC_STEP),
		T(SETTLING_CANCEL_REQUESTED_KILL, R_CANCEL, REC_STEP), X, X},
	[S(STEP_KILLING_COE)] = {
		X, X, X, X, X, X,
		T(SETTLING_CONTINUE_KILL, R_TIMEOUT, REC_STEP),
		T(SETTLING_CONTINUE_KILL, R_TIMEOUT, REC_STEP), X,
		T(SETTLING_CANCEL_TIMEOUT_KILL, R_TIMEOUT, REC_STEP),
		T(SETTLING_CANCEL_REQUESTED_KILL, R_CANCEL, REC_STEP), X, X},
	/* settling-*: the outcome is decided; wait for the step's group to empty.
	 * -term: SIGTERM sent, grace running. -kill: SIGKILL sent. */
	[S(SETTLING_CONTINUE_TERM)] = {
		X, X, X, X, X, X,
		I(SETTLING_CONTINUE_TERM, STALE_LEADER), I(SETTLING_CONTINUE_TERM, STALE_LEADER),
		I(SETTLING_CONTINUE_TERM, STALE_STEP_TIMER),
		T(SETTLING_CANCEL_TIMEOUT_TERM, R_TIMEOUT, NO),
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, NO),
		T(SETTLING_CONTINUE_KILL, R_NONE, KILL),
		T(BETWEEN_STEPS, R_NONE, NO)},
	[S(SETTLING_CONTINUE_KILL)] = {
		X, X, X, X, X, X,
		I(SETTLING_CONTINUE_KILL, STALE_LEADER), I(SETTLING_CONTINUE_KILL, STALE_LEADER),
		I(SETTLING_CONTINUE_KILL, STALE_STEP_TIMER),
		T(SETTLING_CANCEL_TIMEOUT_KILL, R_TIMEOUT, NO),
		T(SETTLING_CANCEL_REQUESTED_KILL, R_CANCEL, NO), X,
		T(BETWEEN_STEPS, R_NONE, NO)},
	[S(SETTLING_FAIL_TERM)] = {
		X, X, X, X, X, X,
		I(SETTLING_FAIL_TERM, STALE_LEADER), I(SETTLING_FAIL_TERM, STALE_LEADER),
		I(SETTLING_FAIL_TERM, STALE_STEP_TIMER),
		T(SETTLING_CANCEL_TIMEOUT_TERM, R_TIMEOUT, NO),
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, NO),
		T(SETTLING_FAIL_KILL, R_NONE, KILL),
		T(FAILED, R_CARRIED, REC_JOB, END)},
	[S(SETTLING_FAIL_KILL)] = {
		X, X, X, X, X, X,
		I(SETTLING_FAIL_KILL, STALE_LEADER), I(SETTLING_FAIL_KILL, STALE_LEADER),
		I(SETTLING_FAIL_KILL, STALE_STEP_TIMER),
		T(SETTLING_CANCEL_TIMEOUT_KILL, R_TIMEOUT, NO),
		T(SETTLING_CANCEL_REQUESTED_KILL, R_CANCEL, NO), X,
		T(FAILED, R_CARRIED, REC_JOB, END)},
	/* A job timeout fires once, so it cannot arrive again here. A cancel
	 * outranks it: the job's reason becomes cancel-requested. */
	[S(SETTLING_CANCEL_TIMEOUT_TERM)] = {
		X, X, X, X, X, X,
		I(SETTLING_CANCEL_TIMEOUT_TERM, STALE_LEADER), I(SETTLING_CANCEL_TIMEOUT_TERM, STALE_LEADER),
		I(SETTLING_CANCEL_TIMEOUT_TERM, STALE_STEP_TIMER), X,
		T(SETTLING_CANCEL_REQUESTED_TERM, R_CANCEL, NO),
		T(SETTLING_CANCEL_TIMEOUT_KILL, R_NONE, KILL),
		T(CANCELLED, R_TIMEOUT, REC_JOB, END)},
	[S(SETTLING_CANCEL_TIMEOUT_KILL)] = {
		X, X, X, X, X, X,
		I(SETTLING_CANCEL_TIMEOUT_KILL, STALE_LEADER), I(SETTLING_CANCEL_TIMEOUT_KILL, STALE_LEADER),
		I(SETTLING_CANCEL_TIMEOUT_KILL, STALE_STEP_TIMER), X,
		T(SETTLING_CANCEL_REQUESTED_KILL, R_CANCEL, NO), X,
		T(CANCELLED, R_TIMEOUT, REC_JOB, END)},
	/* A cancel wins: neither a job timeout nor a second cancel changes it. */
	[S(SETTLING_CANCEL_REQUESTED_TERM)] = {
		X, X, X, X, X, X,
		I(SETTLING_CANCEL_REQUESTED_TERM, STALE_LEADER), I(SETTLING_CANCEL_REQUESTED_TERM, STALE_LEADER),
		I(SETTLING_CANCEL_REQUESTED_TERM, STALE_STEP_TIMER),
		I(SETTLING_CANCEL_REQUESTED_TERM, "a cancel outranks a job timeout"),
		I(SETTLING_CANCEL_REQUESTED_TERM, "the job is already being cancelled"),
		T(SETTLING_CANCEL_REQUESTED_KILL, R_NONE, KILL),
		T(CANCELLED, R_CANCEL, REC_JOB, END)},
	[S(SETTLING_CANCEL_REQUESTED_KILL)] = {
		X, X, X, X, X, X,
		I(SETTLING_CANCEL_REQUESTED_KILL, STALE_LEADER), I(SETTLING_CANCEL_REQUESTED_KILL, STALE_LEADER),
		I(SETTLING_CANCEL_REQUESTED_KILL, STALE_STEP_TIMER),
		I(SETTLING_CANCEL_REQUESTED_KILL, "a cancel outranks a job timeout"),
		I(SETTLING_CANCEL_REQUESTED_KILL, "the job is already being cancelled"), X,
		T(CANCELLED, R_CANCEL, REC_JOB, END)},
	/* Final states: nothing but the broadcast cancel can still reach them.
	 * (Step-scoped events are dropped by index before the table.) */
	[S(SUCCESS)] = {X, X, X, X, X, X, X, X, X, X, I(SUCCESS, FINISHED), X, X},
	[S(FAILED)] = {X, X, X, X, X, X, X, X, X, X, I(FAILED, FINISHED), X, X},
	[S(SKIPPED)] = {X, X, X, X, X, X, X, X, X, X, I(SKIPPED, FINISHED), X, X},
	[S(CANCELLED)] = {X, X, X, X, X, X, X, X, X, X, I(CANCELLED, FINISHED), X, X},
};

static const char *const state_names[QWE_LC_NSTATES] = {
	"pending", "ready", "between-steps", "step-running", "step-running-coe", "step-stopping",
	"step-stopping-coe", "step-killing", "step-killing-coe", "settling-continue-term",
	"settling-continue-kill", "settling-fail-term", "settling-fail-kill", "settling-cancel-timeout-term",
	"settling-cancel-timeout-kill", "settling-cancel-requested-term", "settling-cancel-requested-kill",
	"success", "failed", "skipped", "cancelled",
};

static const char *const event_names[QWE_LC_NEVENTS] = {
	"needs-met", "needs-failed", "slot-granted", "next-step", "no-more-steps", "start-failed",
	"leader-exit-ok", "leader-exit-fail", "step-timeout", "job-timeout", "cancel", "grace-expired",
	"group-empty",
};

static const char *const action_names[] = {
	"none", "start-job", "spawn-step", "sigterm-group", "arm-grace", "sigkill-group", "record-step",
	"record-job", "end-job",
};

const char *qwe_lc_state_name(enum qwe_lc_state s)
{
	return (unsigned)s < QWE_LC_NSTATES ? state_names[s] : "?";
}

const char *qwe_lc_event_name(enum qwe_lc_event e)
{
	return (unsigned)e < QWE_LC_NEVENTS ? event_names[e] : "?";
}

const char *qwe_lc_action_name(enum qwe_lc_action a)
{
	return (unsigned)a < sizeof action_names / sizeof *action_names ? action_names[a] : "?";
}

const char *qwe_lc_reason_name(enum qwe_lc_reason r)
{
	switch (r) {
	case QWE_LC_REASON_DEPENDENCY_FAILED:
		return "dependency-failed";
	case QWE_LC_REASON_CANCEL_REQUESTED:
		return "cancel-requested";
	case QWE_LC_REASON_TIMEOUT:
		return "timeout";
	case QWE_LC_REASON_ENGINE_ERROR:
		return "engine-error";
	default:
		return NULL;
	}
}

const struct qwe_lc_cell *qwe_lc_cell(enum qwe_lc_state s, enum qwe_lc_event e)
{
	return &table[s][e];
}

enum qwe_lc_state qwe_lc_next(const struct qwe_lc_cell *c, int coe)
{
	return (enum qwe_lc_state)(c->next + (c->coe_by_payload && coe ? 1 : 0));
}

int qwe_lc_state_is_final(enum qwe_lc_state s)
{
	return s >= QWE_LC_SUCCESS;
}

int qwe_lc_state_teardown_started(enum qwe_lc_state s)
{
	return s >= QWE_LC_STEP_STOPPING && s <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL;
}

int qwe_lc_state_is_running(enum qwe_lc_state s)
{
	return s >= QWE_LC_BETWEEN_STEPS && s <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL;
}

int qwe_lc_event_is_stale(enum qwe_lc_state s, enum qwe_lc_event e, long event_step, long live_step)
{
	switch (e) {
	case QWE_LC_EV_LEADER_EXIT_OK:
	case QWE_LC_EV_LEADER_EXIT_FAIL:
	case QWE_LC_EV_STEP_TIMEOUT:
	case QWE_LC_EV_GRACE_EXPIRED:
	case QWE_LC_EV_GROUP_EMPTY:
		return event_step != live_step;
	case QWE_LC_EV_JOB_TIMEOUT:
		return qwe_lc_state_is_final(s);
	default:
		return 0;
	}
}

const char *qwe_lc_step_outcome(const struct qwe_lc_result *r)
{
	if (r->next >= QWE_LC_SETTLING_CANCEL_TIMEOUT_TERM && r->next <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL)
		return "cancelled";
	return r->reason ? "failed" : "success";
}

static qwe_lc_abort_hook abort_hook;
static void *abort_arg;

void qwe_lc_set_abort_hook(qwe_lc_abort_hook fn, void *arg)
{
	abort_hook = fn;
	abort_arg = arg;
}

struct qwe_lc_result qwe_lc_lookup(enum qwe_lc_state s, enum qwe_lc_event e, const struct qwe_lc_payload *p)
{
	static const struct qwe_lc_payload none;
	const struct qwe_lc_cell *c;
	struct qwe_lc_result r;
	int i;

	if ((unsigned)s >= QWE_LC_NSTATES || (unsigned)e >= QWE_LC_NEVENTS || table[s][e].kind == QWE_LC_IMPOSSIBLE ||
	    table[s][e].kind == QWE_LC_UNSET) {
		if (abort_hook)
			abort_hook(s, e, abort_arg);
		fprintf(stderr, "qwe: internal error: event %s cannot happen in state %s\n", qwe_lc_event_name(e),
			qwe_lc_state_name(s));
		abort();
	}
	c = &table[s][e];
	if (!p)
		p = &none;
	r.kind = c->kind;
	r.next = c->kind == QWE_LC_IGNORE ? s : qwe_lc_next(c, p->coe);
	r.reason_kind = c->reason;
	r.reason = c->reason == QWE_LC_REASON_FROM_EVENT ? p->reason
		 : c->reason == QWE_LC_REASON_CARRIED ? p->carried
		 : qwe_lc_reason_name(c->reason);
	for (i = 0; i < QWE_LC_MAX_ACTIONS; i++)
		r.actions[i] = c->actions[i];
	r.why = c->why;
	return r;
}
