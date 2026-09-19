#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/lifecycle.h"
#include "src/kernel/lifecycle_model.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define FOR_ALL_CELLS(s, e) \
	for (s = 0; s < QWE_LC_NSTATES; s++) \
		for (e = 0; e < QWE_LC_NEVENTS; e++)

static int has_action(const struct qwe_lc_cell *c, enum qwe_lc_action a)
{
	int i;

	for (i = 0; i < QWE_LC_MAX_ACTIONS; i++)
		if (c->actions[i] == a)
			return 1;
	return 0;
}

/* 0 continue, 1 fail, 2 cancel-timeout, 3 cancel-requested; -1 if not settling. */
static int settling_family(enum qwe_lc_state s)
{
	return s >= QWE_LC_SETTLING_CONTINUE_TERM && s <= QWE_LC_SETTLING_CANCEL_REQUESTED_KILL
		? (int)(s - QWE_LC_SETTLING_CONTINUE_TERM) / 2
		: -1;
}

static int is_live(enum qwe_lc_state s)
{
	return !qwe_lc_state_is_final(s);
}

/* Layer 1: nothing is left unclassified. */
TEST every_cell_classified(void)
{
	int s, e;

	FOR_ALL_CELLS(s, e)
	{
		const struct qwe_lc_cell *c = qwe_lc_cell(s, e);

		if (c->kind != QWE_LC_TRANSITION && c->kind != QWE_LC_IGNORE && c->kind != QWE_LC_IMPOSSIBLE)
			FAILm(qwe_lc_state_name(s)); /* an unset cell */
		if (c->kind == QWE_LC_IGNORE && (!c->why || !*c->why)) {
			fprintf(stderr, "ignore cell %s x %s has no justification\n", qwe_lc_state_name(s),
				qwe_lc_event_name(e));
			FAIL();
		}
	}
	PASS();
}

/* Layer 2: each rule once, over every cell. */
TEST rules_hold_in_every_cell(void)
{
	int s, e;

	FOR_ALL_CELLS(s, e)
	{
		const struct qwe_lc_cell *c = qwe_lc_cell(s, e);
		enum qwe_lc_state next;
		char where[128];

		snprintf(where, sizeof where, "%s x %s", qwe_lc_state_name(s), qwe_lc_event_name(e));
		if (c->kind == QWE_LC_IMPOSSIBLE)
			continue;
		next = qwe_lc_next(c, 1);

		/* A final state has no transitions. */
		if (qwe_lc_state_is_final(s))
			ASSERTm(where, c->kind != QWE_LC_TRANSITION);

		/* An ignore cell never changes the state and has no actions. */
		if (c->kind == QWE_LC_IGNORE) {
			struct qwe_lc_result r = qwe_lc_lookup(s, e, NULL);
			int i;

			ASSERT_EQm(where, (enum qwe_lc_state)s, r.next);
			for (i = 0; i < QWE_LC_MAX_ACTIONS; i++)
				ASSERT_EQm(where, QWE_LC_ACT_NONE, c->actions[i]);
			continue;
		}

		/* SIGKILL only leaves a state in which SIGTERM has been sent. */
		if (has_action(c, QWE_LC_ACT_SIGKILL_GROUP))
			ASSERTm(where, qwe_lc_state_teardown_started(s));

		/* The grace timer is armed only when leaving a state with no
		 * teardown in progress, and always together with the SIGTERM. */
		if (has_action(c, QWE_LC_ACT_ARM_GRACE)) {
			ASSERTm(where, !qwe_lc_state_teardown_started(s));
			ASSERTm(where, has_action(c, QWE_LC_ACT_SIGTERM_GROUP));
		}
		if (has_action(c, QWE_LC_ACT_SIGTERM_GROUP))
			ASSERTm(where, !qwe_lc_state_teardown_started(s));

		/* From every live state, cancel leads only to a cancel-requested
		 * settling state or skipped. The one other exit is cancelled, from
		 * between-steps, where no step is live and there is nothing to
		 * settle. */
		if (e == QWE_LC_EV_CANCEL && is_live(s)) {
			int ok = next == QWE_LC_SETTLING_CANCEL_REQUESTED_TERM ||
				 next == QWE_LC_SETTLING_CANCEL_REQUESTED_KILL || next == QWE_LC_SKIPPED ||
				 (s == QWE_LC_BETWEEN_STEPS && next == QWE_LC_CANCELLED);

			ASSERTm(where, ok);
			ASSERT_EQm(where, QWE_LC_REASON_CANCEL_REQUESTED, c->reason);
		}

		/* Every transition into failed, skipped or cancelled sets a reason,
		 * and so does one that enters a settling family that will end in one
		 * (moving from -term to -kill keeps the reason already set). */
		if (next == QWE_LC_FAILED || next == QWE_LC_SKIPPED || next == QWE_LC_CANCELLED ||
		    (settling_family(next) >= 1 && settling_family(next) != settling_family(s)))
			ASSERTm(where, c->reason != QWE_LC_REASON_NONE);

		/* leader-exit-fail records the event's reason, unchanged, wherever
		 * the leader's own status decides the step (not after a timeout,
		 * when the step failed by timeout whatever the leader said), and
		 * nothing else takes the event's reason. */
		if (e == QWE_LC_EV_LEADER_EXIT_FAIL && (s == QWE_LC_STEP_RUNNING || s == QWE_LC_STEP_RUNNING_COE))
			ASSERT_EQm(where, QWE_LC_REASON_FROM_EVENT, c->reason);
		if (e != QWE_LC_EV_LEADER_EXIT_FAIL)
			ASSERT(c->reason != QWE_LC_REASON_FROM_EVENT);
	}
	PASS();
}

/* Layer 3: breadth-first exploration from pending, driven by the event model. */
static unsigned char reached[QWE_LC_NSTATES][QWE_LC_NEVENTS];
static unsigned char state_seen[QWE_LC_NSTATES];
static int hit_impossible;

static void explore(void)
{
	enum qwe_lc_state queue[QWE_LC_NSTATES * 2];
	size_t head = 0, tail = 0;

	memset(reached, 0, sizeof reached);
	memset(state_seen, 0, sizeof state_seen);
	hit_impossible = 0;
	queue[tail++] = QWE_LC_PENDING;
	state_seen[QWE_LC_PENDING] = 1;
	while (head < tail) {
		enum qwe_lc_state s = queue[head++];
		unsigned mask = qwe_lcm_events(s);
		int e;

		for (e = 0; e < QWE_LC_NEVENTS; e++) {
			const struct qwe_lc_cell *c;
			int coe;

			if (!(mask & (1u << e)))
				continue;
			c = qwe_lc_cell(s, e);
			reached[s][e] = 1;
			if (c->kind == QWE_LC_IMPOSSIBLE || c->kind == QWE_LC_UNSET) {
				hit_impossible = 1;
				fprintf(stderr, "the model reaches %s x %s, which is not decided\n",
					qwe_lc_state_name(s), qwe_lc_event_name(e));
				continue;
			}
			for (coe = 0; coe <= c->coe_by_payload; coe++) {
				enum qwe_lc_state n = c->kind == QWE_LC_IGNORE ? s : qwe_lc_next(c, coe);

				if (!state_seen[n]) {
					state_seen[n] = 1;
					queue[tail++] = n;
				}
			}
		}
	}
}

TEST impossible_cells_unreachable(void)
{
	explore();
	ASSERT(!hit_impossible);
	PASS();
}

TEST every_possible_cell_reachable(void)
{
	int s, e;
	unsigned events_used = 0;

	explore();
	for (s = 0; s < QWE_LC_NSTATES; s++) {
		if (!state_seen[s]) {
			fprintf(stderr, "state %s is never reached\n", qwe_lc_state_name(s));
			FAIL();
		}
		for (e = 0; e < QWE_LC_NEVENTS; e++) {
			const struct qwe_lc_cell *c = qwe_lc_cell(s, e);

			if (reached[s][e])
				events_used |= 1u << e;
			if (c->kind != QWE_LC_IMPOSSIBLE && !reached[s][e]) {
				fprintf(stderr, "%s x %s is decided but the model never reaches it\n",
					qwe_lc_state_name(s), qwe_lc_event_name(e));
				FAIL();
			}
		}
	}
	ASSERT_EQ((1u << QWE_LC_NEVENTS) - 1, events_used);
	PASS();
}

/* Layer 4: named scenarios. */
struct step {
	enum qwe_lc_event event;
	struct qwe_lc_payload payload;
	enum qwe_lc_state want_next;
	const char *want_reason;
	enum qwe_lc_kind want_kind;
};

#define EVT(e) QWE_LC_EV_##e
#define ST(s) QWE_LC_##s
/* An event, the state it leads to, and the reason recorded. */
#define GO(e, next, reason) {EVT(e), {0, NULL, NULL}, ST(next), reason, QWE_LC_TRANSITION}
#define GO_P(e, pl, next, reason) {EVT(e), pl, ST(next), reason, QWE_LC_TRANSITION}
#define STALE(e, state) {EVT(e), {0, NULL, NULL}, ST(state), NULL, QWE_LC_IGNORE}
#define COE {1, NULL, NULL}
#define PLAIN {0, NULL, NULL}
#define FAILS(r) {0, r, NULL}
#define CARRY(r) {0, NULL, r}

static enum qwe_lc_state walk(enum qwe_lc_state s, const struct step *steps, size_t n, char *why, size_t whylen)
{
	size_t i;

	for (i = 0; i < n; i++) {
		struct qwe_lc_result r = qwe_lc_lookup(s, steps[i].event, &steps[i].payload);

		if (r.kind != steps[i].want_kind || r.next != steps[i].want_next ||
		    (r.reason ? (!steps[i].want_reason || strcmp(r.reason, steps[i].want_reason) != 0)
			      : steps[i].want_reason != NULL)) {
			snprintf(why, whylen, "step %zu: %s x %s gave %s (reason %s), wanted %s (reason %s)", i,
				 qwe_lc_state_name(s), qwe_lc_event_name(steps[i].event), qwe_lc_state_name(r.next),
				 r.reason ? r.reason : "none", qwe_lc_state_name(steps[i].want_next),
				 steps[i].want_reason ? steps[i].want_reason : "none");
			return QWE_LC_NSTATES;
		}
		s = r.next;
	}
	return s;
}

#define SCENARIO(name, start, end, ...) \
	TEST scenario_##name(void) \
	{ \
		static const struct step steps[] = {__VA_ARGS__}; \
		char why[256] = ""; \
		enum qwe_lc_state got = walk(start, steps, sizeof steps / sizeof *steps, why, sizeof why); \
		ASSERTm(why, got != QWE_LC_NSTATES); \
		ASSERT_EQ_FMT((enum qwe_lc_state)end, got, "%d"); \
		PASS(); \
	}

SCENARIO(clean_run_two_steps, QWE_LC_PENDING, QWE_LC_SUCCESS,
	 GO(NEEDS_MET, READY, NULL), GO(SLOT_GRANTED, BETWEEN_STEPS, NULL),
	 GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL), GO(LEADER_EXIT_OK, SETTLING_CONTINUE_TERM, NULL),
	 GO(GROUP_EMPTY, BETWEEN_STEPS, NULL), GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL),
	 GO(LEADER_EXIT_OK, SETTLING_CONTINUE_TERM, NULL), GO(GROUP_EMPTY, BETWEEN_STEPS, NULL),
	 GO(NO_MORE_STEPS, SUCCESS, NULL))

SCENARIO(step_fails_without_coe, QWE_LC_BETWEEN_STEPS, QWE_LC_FAILED,
	 GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL),
	 GO_P(LEADER_EXIT_FAIL, FAILS("exit-code"), SETTLING_FAIL_TERM, "exit-code"),
	 GO_P(GROUP_EMPTY, CARRY("exit-code"), FAILED, "exit-code"))

SCENARIO(step_fails_with_coe, QWE_LC_BETWEEN_STEPS, QWE_LC_SUCCESS,
	 GO_P(NEXT_STEP, COE, STEP_RUNNING_COE, NULL),
	 GO_P(LEADER_EXIT_FAIL, FAILS("exit-code"), SETTLING_CONTINUE_TERM, "exit-code"),
	 GO(GROUP_EMPTY, BETWEEN_STEPS, NULL), GO(NO_MORE_STEPS, SUCCESS, NULL))

SCENARIO(step_timeout_then_grace_expired, QWE_LC_BETWEEN_STEPS, QWE_LC_FAILED,
	 GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL), GO(STEP_TIMEOUT, STEP_STOPPING, "timeout"),
	 GO(GRACE_EXPIRED, STEP_KILLING, NULL), GO(LEADER_EXIT_FAIL, SETTLING_FAIL_KILL, "timeout"),
	 GO_P(GROUP_EMPTY, CARRY("timeout"), FAILED, "timeout"))

SCENARIO(step_timeout_with_coe_continues, QWE_LC_BETWEEN_STEPS, QWE_LC_BETWEEN_STEPS,
	 GO_P(NEXT_STEP, COE, STEP_RUNNING_COE, NULL), GO(STEP_TIMEOUT, STEP_STOPPING_COE, "timeout"),
	 GO(LEADER_EXIT_FAIL, SETTLING_CONTINUE_TERM, "timeout"), GO(GROUP_EMPTY, BETWEEN_STEPS, NULL))

SCENARIO(step_timeout_then_job_timeout, QWE_LC_BETWEEN_STEPS, QWE_LC_CANCELLED,
	 GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL), GO(STEP_TIMEOUT, STEP_STOPPING, "timeout"),
	 GO(JOB_TIMEOUT, SETTLING_CANCEL_TIMEOUT_TERM, "timeout"),
	 STALE(LEADER_EXIT_FAIL, SETTLING_CANCEL_TIMEOUT_TERM), GO(GROUP_EMPTY, CANCELLED, "timeout"))

SCENARIO(job_timeout_then_cancel, QWE_LC_BETWEEN_STEPS, QWE_LC_CANCELLED,
	 GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL), GO(JOB_TIMEOUT, SETTLING_CANCEL_TIMEOUT_TERM, "timeout"),
	 GO(CANCEL, SETTLING_CANCEL_REQUESTED_TERM, "cancel-requested"),
	 STALE(JOB_TIMEOUT, SETTLING_CANCEL_REQUESTED_TERM), GO(GRACE_EXPIRED, SETTLING_CANCEL_REQUESTED_KILL, NULL),
	 GO(GROUP_EMPTY, CANCELLED, "cancel-requested"))

SCENARIO(stragglers_outlive_a_successful_leader, QWE_LC_BETWEEN_STEPS, QWE_LC_SUCCESS,
	 GO_P(NEXT_STEP, PLAIN, STEP_RUNNING, NULL), GO(LEADER_EXIT_OK, SETTLING_CONTINUE_TERM, NULL),
	 GO(GRACE_EXPIRED, SETTLING_CONTINUE_KILL, NULL), GO(GROUP_EMPTY, BETWEEN_STEPS, NULL),
	 GO(NO_MORE_STEPS, SUCCESS, NULL))

SCENARIO(start_failed_is_engine_error, QWE_LC_BETWEEN_STEPS, QWE_LC_FAILED,
	 GO_P(NEXT_STEP, COE, STEP_RUNNING_COE, NULL), GO(START_FAILED, FAILED, "engine-error"))

SCENARIO(job_start_failed_is_engine_error, QWE_LC_READY, QWE_LC_FAILED,
	 GO(SLOT_GRANTED, BETWEEN_STEPS, NULL), GO(START_FAILED, FAILED, "engine-error"))

SCENARIO(needs_failed_skips, QWE_LC_PENDING, QWE_LC_SKIPPED, GO(NEEDS_FAILED, SKIPPED, "dependency-failed"))

SCENARIO(cancel_in_pending_skips, QWE_LC_PENDING, QWE_LC_SKIPPED, GO(CANCEL, SKIPPED, "cancel-requested"))

SCENARIO(cancel_in_ready_skips, QWE_LC_READY, QWE_LC_SKIPPED, GO(CANCEL, SKIPPED, "cancel-requested"))

TEST scenario_actions_of_a_step_timeout(void)
{
	struct qwe_lc_result r = qwe_lc_lookup(QWE_LC_STEP_RUNNING, QWE_LC_EV_STEP_TIMEOUT, NULL);

	ASSERT_EQ(QWE_LC_ACT_SIGTERM_GROUP, r.actions[0]);
	ASSERT_EQ(QWE_LC_ACT_ARM_GRACE, r.actions[1]);
	r = qwe_lc_lookup(QWE_LC_STEP_STOPPING, QWE_LC_EV_JOB_TIMEOUT, NULL);
	ASSERT_EQ(QWE_LC_ACT_RECORD_STEP, r.actions[0]); /* no second SIGTERM, no second grace */
	ASSERT_EQ(QWE_LC_ACT_NONE, r.actions[1]);
	PASS();
}

TEST impossible_cell_aborts(void)
{
	/* The impossible lookup aborts, so it runs in a child. */
	pid_t pid = fork();
	int st;

	ASSERT(pid >= 0);
	if (pid == 0) {
		close(2); /* keep the expected message out of the test log */
		qwe_lc_lookup(QWE_LC_PENDING, QWE_LC_EV_GRACE_EXPIRED, NULL);
		_exit(0); /* not reached */
	}
	ASSERT_EQ(pid, waitpid(pid, &st, 0));
	ASSERT(WIFSIGNALED(st));
	ASSERT_EQ(SIGABRT, WTERMSIG(st));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(every_cell_classified);
	RUN_TEST(rules_hold_in_every_cell);
	RUN_TEST(impossible_cells_unreachable);
	RUN_TEST(every_possible_cell_reachable);
	RUN_TEST(scenario_clean_run_two_steps);
	RUN_TEST(scenario_step_fails_without_coe);
	RUN_TEST(scenario_step_fails_with_coe);
	RUN_TEST(scenario_step_timeout_then_grace_expired);
	RUN_TEST(scenario_step_timeout_with_coe_continues);
	RUN_TEST(scenario_step_timeout_then_job_timeout);
	RUN_TEST(scenario_job_timeout_then_cancel);
	RUN_TEST(scenario_stragglers_outlive_a_successful_leader);
	RUN_TEST(scenario_start_failed_is_engine_error);
	RUN_TEST(scenario_job_start_failed_is_engine_error);
	RUN_TEST(scenario_needs_failed_skips);
	RUN_TEST(scenario_cancel_in_pending_skips);
	RUN_TEST(scenario_cancel_in_ready_skips);
	RUN_TEST(scenario_actions_of_a_step_timeout);
	RUN_TEST(impossible_cell_aborts);
	GREATEST_MAIN_END();
}
