#include "greatest.h"
#include "src/kernel/dag.h"

#include <string.h>

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
	RUN_TEST(diamond_is_fine);
	GREATEST_MAIN_END();
}
