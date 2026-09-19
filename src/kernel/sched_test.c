#include "greatest.h"
#include "src/kernel/sched.h"

#include <stdlib.h>

#define MAXJ 12

struct graph {
	enum qwe_job_state state[MAXJ];
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
		g->state[i] = QWE_JOB_PENDING;
		g->jobs[i].state = &g->state[i];
		g->jobs[i].needs = g->needs[i];
		g->jobs[i].nneeds = 0;
		for (k = 0; k < i; k++)
			if (rand() % 3 == 0)
				g->needs[i][g->jobs[i].nneeds++] = k;
	}
}

static size_t count_running(const struct graph *g)
{
	size_t i, r = 0;

	for (i = 0; i < g->n; i++)
		r += g->state[i] == QWE_JOB_RUNNING;
	return r;
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
			size_t starts[MAXJ], got, s;
			int all_final = 1;

			got = qwe_sched_pass(g.jobs, g.n, max, starts);
			for (s = 0; s < got; s++) {
				ASSERT_EQ(QWE_JOB_READY, g.state[starts[s]]);
				qwe_job_transition(&g.state[starts[s]], QWE_JOB_RUNNING);
			}
			if (max > 0)
				ASSERT(count_running(&g) <= (size_t)max);

			/* Something running finishes, at random: success or failure. */
			for (i = 0; i < g.n; i++)
				if (g.state[i] == QWE_JOB_RUNNING && rand() % 2 == 0)
					qwe_job_transition(&g.state[i], rand() % 4 ? QWE_JOB_SUCCESS : QWE_JOB_FAILED);
			for (i = 0; i < g.n; i++)
				all_final = all_final && qwe_job_state_is_final(g.state[i]);
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
	enum qwe_job_state st[3] = {QWE_JOB_FAILED, QWE_JOB_PENDING, QWE_JOB_PENDING};
	struct qwe_sched_job jobs[3] = {{&st[0], NULL, 0}, {&st[1], b_needs, 1}, {&st[2], c_needs, 1}};
	size_t starts[3];

	ASSERT_EQ(0, (int)qwe_sched_pass(jobs, 3, 0, starts));
	ASSERT_EQ(QWE_JOB_SKIPPED, st[1]);
	ASSERT_EQ(QWE_JOB_SKIPPED, st[2]); /* the skip cascades in one pass */
	PASS();
}

TEST starts_in_index_order(void)
{
	enum qwe_job_state st[3] = {QWE_JOB_PENDING, QWE_JOB_PENDING, QWE_JOB_PENDING};
	struct qwe_sched_job jobs[3] = {{&st[0], NULL, 0}, {&st[1], NULL, 0}, {&st[2], NULL, 0}};
	size_t starts[3];

	ASSERT_EQ(2, (int)qwe_sched_pass(jobs, 3, 2, starts));
	ASSERT_EQ(0, (int)starts[0]);
	ASSERT_EQ(1, (int)starts[1]);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(never_exceeds_max_parallel);
	RUN_TEST(skips_when_a_need_fails);
	RUN_TEST(starts_in_index_order);
	GREATEST_MAIN_END();
}
