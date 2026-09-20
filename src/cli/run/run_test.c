#include "greatest.h"
#include "src/cli/run/run.h"
#include "src/kernel/qwe.h"

/* Nothing of these reaches the engine: each is refused while the arguments are read. */
TEST option_parsing(void)
{
	/* There is one guarantee level: connecting up front is not optional. */
	char *no_preconnect[] = {"run", "--no-preconnect", "w.yaml"};
	char *no_preconnect_after[] = {"run", "w.yaml", "--no-preconnect"};
	char *unknown[] = {"run", "--bogus", "w.yaml"};
	char *no_file[] = {"run", "--debug"};
	char *two_files[] = {"run", "a.yaml", "b.yaml"};
	char *job_needs_id[] = {"run", "w.yaml", "--job"};
	char *inv_needs_path[] = {"run", "w.yaml", "-i"};
	char *inv_twice[] = {"run", "w.yaml", "-i", "a.yaml", "-i", "b.yaml"};
	char *unknown_after_file[] = {"run", "w.yaml", "--job", "a", "--bogus"};

	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(3, no_preconnect));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(3, no_preconnect_after));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(3, unknown));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(2, no_file));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(3, two_files));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(3, job_needs_id));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(3, inv_needs_path));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(6, inv_twice));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_run(5, unknown_after_file));
	PASS();
}

SUITE(run)
{
	RUN_TEST(option_parsing);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(run);
	GREATEST_MAIN_END();
}
