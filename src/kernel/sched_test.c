#include "greatest.h"
#include "src/kernel/sched.h"

#include <stdlib.h>

#define MAXJ 12

struct graph {
	enum qwe_lc_state state[MAXJ];
	size_t needs[MAXJ][MAXJ];
	struct qwe_sched_job jobs[MAXJ];
	size_t n;
};

/* A random DAG: a job only needs lower-numbered jobs. */
static void random_graph(struct graph *g)
{
	size_t i, k;

	g->n = 1 + (size_t)rand() % MAXJ;
	for (i = 0; i < g->n; i++) {
		g->state[i] = QWE_LC_PENDING;
		g->jobs[i].state = &g->state[i];
		g->jobs[i].needs = g->needs[i];
		g->jobs[i].nneeds = 0;
		g->jobs[i].group = 0;
		for (k = 0; k < i; k++)
			if (rand() % 3 == 0)
				g->needs[i][g->jobs[i].nneeds++] = k;
	}
}

static size_t count_running(const struct graph *g)
{
	size_t i, r = 0;

	for (i = 0; i < g->n; i++)
		r += qwe_lc_state_is_running(g->state[i]);
	return r;
}

/* What the shell does with the events: the state each one leads to. (Just
 * enough of the table for the scheduler's three events.) */
static void send(struct graph *g, const struct qwe_sched_event *ev)
{
	enum qwe_lc_state *s = &g->state[ev->job];

	*s = qwe_lc_lookup(*s, ev->event, NULL).next;
}

TEST never_exceeds_max_parallel(void)
{
	int trial;

	srand(20260919);
	for (trial = 0; trial < 2000; trial++) {
		struct graph g;
		long max = rand() % 5; /* 0 is unlimited */
		size_t i, guard;

		random_graph(&g);
		for (guard = 0; guard < 1000; guard++) {
			struct qwe_sched_event evs[MAXJ];
			size_t got, e;
			int all_final = 1;

			while ((got = qwe_sched_pass(g.jobs, g.n, max, NULL, evs)) > 0) {
				for (e = 0; e < got; e++)
					send(&g, &evs[e]);
				if (max > 0)
					ASSERT(count_running(&g) <= (size_t)max);
			}

			/* Something running finishes, at random: success or failure. */
			for (i = 0; i < g.n; i++)
				if (qwe_lc_state_is_running(g.state[i]) && rand() % 2 == 0)
					g.state[i] = rand() % 4 ? QWE_LC_SUCCESS : QWE_LC_FAILED;
			for (i = 0; i < g.n; i++)
				all_final = all_final && qwe_lc_state_is_final(g.state[i]);
			if (all_final)
				break;
		}
		ASSERT(guard < 1000); /* it always makes progress */
	}
	PASS();
}

TEST skips_when_a_need_fails(void)
{
	static const size_t b_needs[] = {0}, c_needs[] = {1};
	enum qwe_lc_state st[3] = {QWE_LC_FAILED, QWE_LC_PENDING, QWE_LC_PENDING};
	struct qwe_sched_job jobs[3] = {{&st[0], NULL, 0, 0}, {&st[1], b_needs, 1, 0}, {&st[2], c_needs, 1, 0}};
	struct qwe_sched_event evs[3];

	/* b is told at once; c can only be told after b has ended. */
	ASSERT_EQ(1, (int)qwe_sched_pass(jobs, 3, 0, NULL, evs));
	ASSERT_EQ(1, (int)evs[0].job);
	ASSERT_EQ(QWE_LC_EV_NEEDS_FAILED, evs[0].event);
	st[1] = qwe_lc_lookup(st[1], evs[0].event, NULL).next;
	ASSERT_EQ(QWE_LC_SKIPPED, st[1]);
	ASSERT_EQ(1, (int)qwe_sched_pass(jobs, 3, 0, NULL, evs));
	ASSERT_EQ(2, (int)evs[0].job);
	ASSERT_EQ(QWE_LC_EV_NEEDS_FAILED, evs[0].event);
	PASS();
}

TEST starts_in_index_order(void)
{
	enum qwe_lc_state st[3] = {QWE_LC_READY, QWE_LC_READY, QWE_LC_READY};
	struct qwe_sched_job jobs[3] = {{&st[0], NULL, 0, 0}, {&st[1], NULL, 0, 0}, {&st[2], NULL, 0, 0}};
	struct qwe_sched_event evs[3];

	ASSERT_EQ(2, (int)qwe_sched_pass(jobs, 3, 2, NULL, evs));
	ASSERT_EQ(0, (int)evs[0].job);
	ASSERT_EQ(1, (int)evs[1].job);
	ASSERT_EQ(QWE_LC_EV_SLOT_GRANTED, evs[0].event);
	PASS();
}

TEST needs_met_comes_before_slots(void)
{
	/* A job that has just become ready must not lose its turn to a
	 * higher-numbered one already waiting: needs events go first. */
	static const size_t a_needs[] = {2};
	enum qwe_lc_state st[3] = {QWE_LC_PENDING, QWE_LC_READY, QWE_LC_SUCCESS};
	struct qwe_sched_job jobs[3] = {{&st[0], a_needs, 1, 0}, {&st[1], NULL, 0, 0}, {&st[2], NULL, 0, 0}};
	struct qwe_sched_event evs[3];

	ASSERT_EQ(1, (int)qwe_sched_pass(jobs, 3, 1, NULL, evs));
	ASSERT_EQ(0, (int)evs[0].job);
	ASSERT_EQ(QWE_LC_EV_NEEDS_MET, evs[0].event);
	PASS();
}

TEST session_cap_makes_jobs_wait(void)
{
	/* a and b share target 1, whose cap is 1: with a running, b waits, and c
	 * (no group) is not held back by it. */
	enum qwe_lc_state st[3] = {QWE_LC_BETWEEN_STEPS, QWE_LC_READY, QWE_LC_READY};
	struct qwe_sched_job jobs[3] = {{&st[0], NULL, 0, 1}, {&st[1], NULL, 0, 1}, {&st[2], NULL, 0, 0}};
	static const long caps[] = {1};
	struct qwe_sched_event evs[3];

	ASSERT_EQ(1, (int)qwe_sched_pass(jobs, 3, 0, caps, evs));
	ASSERT_EQ(2, (int)evs[0].job);
	/* a ends: b gets the session, and only one of two waiting jobs of the group is granted */
	st[0] = QWE_LC_SUCCESS;
	st[2] = QWE_LC_READY;
	jobs[2].group = 1;
	ASSERT_EQ(1, (int)qwe_sched_pass(jobs, 3, 0, caps, evs));
	ASSERT_EQ(1, (int)evs[0].job);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(never_exceeds_max_parallel);
	RUN_TEST(skips_when_a_need_fails);
	RUN_TEST(starts_in_index_order);
	RUN_TEST(needs_met_comes_before_slots);
	RUN_TEST(session_cap_makes_jobs_wait);
	GREATEST_MAIN_END();
}
