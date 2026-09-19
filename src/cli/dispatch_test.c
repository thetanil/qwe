#include "greatest.h"
#include "src/cli/dispatch.h"

#include <string.h>

TEST known_subcommands_resolve(void)
{
	static const char *const names[] = {"run", "validate", "encrypt", "keygen", "serve"};
	size_t i;

	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		const struct qwe_subcommand *cmd = qwe_find_subcommand(names[i]);
		ASSERT(cmd != NULL);
		ASSERT_STR_EQ(names[i], cmd->name);
		ASSERT(cmd->fn != NULL);
	}
	PASS();
}

TEST unknown_subcommand_does_not_resolve(void)
{
	ASSERT_EQ(NULL, qwe_find_subcommand("bogus"));
	ASSERT_EQ(NULL, qwe_find_subcommand(""));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(known_subcommands_resolve);
	RUN_TEST(unknown_subcommand_does_not_resolve);
	GREATEST_MAIN_END();
}
