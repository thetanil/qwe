/* The job lifecycle (ADR-0010): one flat table, (state, event) -> cell.
 *
 * Pure: no I/O, no allocation, nothing beyond libc. The event loop turns fds
 * and signals into events and carries out the actions a lookup returns; it
 * decides nothing itself. */
#ifndef QWE_KERNEL_LIFECYCLE_H
#define QWE_KERNEL_LIFECYCLE_H

/* A coe state is always its plain state + 1 (see qwe_lc_next). */
enum qwe_lc_state {
	QWE_LC_PENDING,
	QWE_LC_READY,
	QWE_LC_BETWEEN_STEPS,
	QWE_LC_STEP_RUNNING,
	QWE_LC_STEP_RUNNING_COE,
	QWE_LC_STEP_STOPPING, /* step timeout: TERM sent, grace armed */
	QWE_LC_STEP_STOPPING_COE,
	QWE_LC_STEP_KILLING, /* grace expired: KILL sent */
	QWE_LC_STEP_KILLING_COE,
	QWE_LC_SETTLING_CONTINUE_TERM, /* outcome decided; waiting for the group to empty */
	QWE_LC_SETTLING_CONTINUE_KILL,
	QWE_LC_SETTLING_FAIL_TERM,
	QWE_LC_SETTLING_FAIL_KILL,
	QWE_LC_SETTLING_CANCEL_TIMEOUT_TERM,
	QWE_LC_SETTLING_CANCEL_TIMEOUT_KILL,
	QWE_LC_SETTLING_CANCEL_REQUESTED_TERM,
	QWE_LC_SETTLING_CANCEL_REQUESTED_KILL,
	QWE_LC_SUCCESS,
	QWE_LC_FAILED,
	QWE_LC_SKIPPED,
	QWE_LC_CANCELLED,
	QWE_LC_NSTATES
};

enum qwe_lc_event {
	QWE_LC_EV_NEEDS_MET,
	QWE_LC_EV_NEEDS_FAILED,
	QWE_LC_EV_SLOT_GRANTED,
	QWE_LC_EV_NEXT_STEP, /* payload: coe */
	QWE_LC_EV_NO_MORE_STEPS,
	QWE_LC_EV_START_FAILED,
	QWE_LC_EV_LEADER_EXIT_OK,
	QWE_LC_EV_LEADER_EXIT_FAIL, /* payload: the step's reason */
	QWE_LC_EV_STEP_TIMEOUT,
	QWE_LC_EV_JOB_TIMEOUT,
	QWE_LC_EV_CANCEL,
	QWE_LC_EV_GRACE_EXPIRED,
	QWE_LC_EV_GROUP_EMPTY,
	QWE_LC_NEVENTS
};

enum qwe_lc_action {
	QWE_LC_ACT_NONE, /* pads the action array */
	QWE_LC_ACT_START_JOB, /* open the log, arm the job timeout */
	QWE_LC_ACT_SPAWN_STEP,
	QWE_LC_ACT_SIGTERM_GROUP,
	QWE_LC_ACT_ARM_GRACE,
	QWE_LC_ACT_SIGKILL_GROUP,
	QWE_LC_ACT_RECORD_STEP,
	QWE_LC_ACT_RECORD_JOB,
	QWE_LC_ACT_END_JOB /* release the job's resources; unstarted steps are skipped */
};

enum qwe_lc_kind {
	QWE_LC_UNSET, /* zero: a cell nobody classified */
	QWE_LC_TRANSITION,
	QWE_LC_IGNORE, /* stale and harmless; the cell says why */
	QWE_LC_IMPOSSIBLE /* a kernel bug: qwe_lc_lookup aborts */
};

enum qwe_lc_reason {
	QWE_LC_REASON_NONE,
	QWE_LC_REASON_DEPENDENCY_FAILED,
	QWE_LC_REASON_CANCEL_REQUESTED,
	QWE_LC_REASON_TIMEOUT,
	QWE_LC_REASON_ENGINE_ERROR,
	QWE_LC_REASON_FROM_EVENT, /* the event's own reason, recorded unchanged */
	QWE_LC_REASON_CARRIED /* the reason recorded when the outcome was decided */
};

#define QWE_LC_MAX_ACTIONS 4

struct qwe_lc_cell {
	enum qwe_lc_kind kind;
	enum qwe_lc_state next;
	enum qwe_lc_reason reason;
	/* next is the plain state, or its coe variant if the event says coe */
	int coe_by_payload;
	enum qwe_lc_action actions[QWE_LC_MAX_ACTIONS];
	const char *why; /* ignore cells: the justification */
};

/* What an event carries. */
struct qwe_lc_payload {
	int coe; /* next-step: the step's continue-on-error */
	const char *reason; /* leader-exit-fail: the step's reason */
	const char *carried; /* the reason recorded earlier, for CARRIED cells */
};

/* A cell with the payload applied. */
struct qwe_lc_result {
	enum qwe_lc_kind kind;
	enum qwe_lc_state next;
	enum qwe_lc_reason reason_kind;
	const char *reason; /* the reason's text, or NULL for none */
	enum qwe_lc_action actions[QWE_LC_MAX_ACTIONS];
	const char *why;
};

/* The table's own cell, unchanged. Never aborts, so tests can look at every
 * cell. */
const struct qwe_lc_cell *qwe_lc_cell(enum qwe_lc_state s, enum qwe_lc_event e);

/* The cell's next state, with the payload's coe applied. */
enum qwe_lc_state qwe_lc_next(const struct qwe_lc_cell *c, int coe);

/* The lookup. An impossible cell is a kernel bug: it names the state and the
 * event on stderr and aborts (in every build type). */
struct qwe_lc_result qwe_lc_lookup(enum qwe_lc_state s, enum qwe_lc_event e, const struct qwe_lc_payload *p);

const char *qwe_lc_state_name(enum qwe_lc_state s);
const char *qwe_lc_event_name(enum qwe_lc_event e);
const char *qwe_lc_action_name(enum qwe_lc_action a);
/* The text of a fixed reason ("timeout", ...), or NULL for none, FROM_EVENT
 * and CARRIED. */
const char *qwe_lc_reason_name(enum qwe_lc_reason r);

int qwe_lc_state_is_final(enum qwe_lc_state s);
/* True when SIGTERM has been sent to the step's group and the grace timer is
 * running or expired: the stopping, killing and settling states. */
int qwe_lc_state_teardown_started(enum qwe_lc_state s);

#endif
