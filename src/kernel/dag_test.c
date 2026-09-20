#include "greatest.h"
#include "src/kernel/dag.h"

#include <stdlib.h>
#include <string.h>

void *__real_calloc(size_t a, size_t b);

static int fail_calloc; /* armed: every calloc returns NULL */

void *__wrap_calloc(size_t a, size_t b)
{
	return fail_calloc ? NULL : __real_calloc(a, b);
}

TEST cycle_detected(void)
{
	static const char *const a_needs[] = {"c"};
	static const char *const b_needs[] = {"a"};
	static const char *const c_needs[] = {"b"};
	static const char *const d_needs[] = {"a"}; /* hangs off the cycle, is not in it */
	const struct qwe_dag_job jobs[] = {
		{"a", a_needs, 1}, {"b", b_needs, 1}, {"c", c_needs, 1}, {"d", d_needs, 1},
	};
	struct qwe_dag_error err;

	ASSERT_EQ(QWE_DAG_CYCLE, qwe_dag_check(jobs, 4, &err));
	ASSERT_EQ(0, (int)err.job); /* the cycle is entered at "a" */
	ASSERT_STR_EQ("needs cycle: a -> c -> b -> a", err.message);
	PASS();
}

TEST self_need_is_a_cycle(void)
{
	static const char *const needs[] = {"a"};
	const struct qwe_dag_job jobs[] = {{"a", needs, 1}};
	struct qwe_dag_error err;

	ASSERT_EQ(QWE_DAG_CYCLE, qwe_dag_check(jobs, 1, &err));
	ASSERT_STR_EQ("needs cycle: a -> a", err.message);
	PASS();
}

TEST unknown_need(void)
{
	static const char *const b_needs[] = {"a", "nope"};
	const struct qwe_dag_job jobs[] = {{"a", NULL, 0}, {"b", b_needs, 2}};
	struct qwe_dag_error err;

	ASSERT_EQ(QWE_DAG_UNKNOWN_NEED, qwe_dag_check(jobs, 2, &err));
	ASSERT_EQ(1, (int)err.job);
	ASSERT_EQ(1, (int)err.need);
	ASSERT(strstr(err.message, "\"b\"") != NULL);
	ASSERT(strstr(err.message, "\"nope\"") != NULL);
	PASS();
}

TEST allocation_failure_is_not_ok(void)
{
	static const char *const a_needs[] = {"b"};
	static const char *const b_needs[] = {"a"};
	const struct qwe_dag_job cycle[] = {{"a", a_needs, 1}, {"b", b_needs, 1}};
	const struct qwe_dag_job plain[] = {{"a", NULL, 0}};
	struct qwe_dag_error err;

	/* A graph that could not be checked is never reported as a good one, cyclic or not. */
	fail_calloc = 1;
	ASSERT_EQ(QWE_DAG_NO_MEMORY, qwe_dag_check(cycle, 2, &err));
	ASSERT(strstr(err.message, "out of memory") != NULL);
	ASSERT_EQ(QWE_DAG_NO_MEMORY, qwe_dag_check(plain, 1, &err));
	ASSERT_EQ(QWE_DAG_NO_MEMORY, qwe_dag_check(plain, 1, NULL));
	fail_calloc = 0;
	/* and with memory the same graphs are judged as before */
	ASSERT_EQ(QWE_DAG_CYCLE, qwe_dag_check(cycle, 2, &err));
	ASSERT_EQ(QWE_DAG_OK, qwe_dag_check(plain, 1, &err));
	PASS();
}

TEST diamond_is_fine(void)
{
	static const char *const b_needs[] = {"a"};
	static const char *const c_needs[] = {"a"};
	static const char *const d_needs[] = {"b", "c"};
	const struct qwe_dag_job jobs[] = {
		{"a", NULL, 0}, {"b", b_needs, 1}, {"c", c_needs, 1}, {"d", d_needs, 2},
	};

	ASSERT_EQ(QWE_DAG_OK, qwe_dag_check(jobs, 4, NULL));
	ASSERT_EQ(QWE_DAG_OK, qwe_dag_check(jobs, 0, NULL));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(cycle_detected);
	RUN_TEST(self_need_is_a_cycle);
	RUN_TEST(unknown_need);
	RUN_TEST(allocation_failure_is_not_ok);
	RUN_TEST(diamond_is_fine);
	GREATEST_MAIN_END();
}
