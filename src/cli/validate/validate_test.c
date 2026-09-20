#include "greatest.h"
#include "src/cli/validate/validate.h"
#include "src/kernel/qwe.h"

/* Nothing of these reaches the engine: each is refused while the arguments are read. */
TEST option_parsing(void)
{
	char *unknown[] = {"validate", "--bogus", "w.yaml"};
	char *no_file[] = {"validate"};
	char *only_inventory[] = {"validate", "-i", "inv.yaml"};
	char *two_files[] = {"validate", "a.yaml", "b.yaml"};
	char *inv_needs_path[] = {"validate", "w.yaml", "-i"};
	char *inv_twice[] = {"validate", "w.yaml", "-i", "a.yaml", "-i", "b.yaml"};

	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_validate(3, unknown));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_validate(1, no_file));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_validate(3, only_inventory));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_validate(3, two_files));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_validate(3, inv_needs_path));
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_cmd_validate(6, inv_twice));
	PASS();
}

SUITE(validate)
{
	RUN_TEST(option_parsing);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(validate);
	GREATEST_MAIN_END();
}
