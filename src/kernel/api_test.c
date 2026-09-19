/* This binary supplies its own main. If the kernel library defined one, the
 * link would fail with a duplicate symbol. */
#include "greatest.h"
#include "src/kernel/qwe.h"

#include <string.h>

TEST links_without_cli(void)
{
	ASSERT_STR_EQ("qwe " QWE_VERSION, qwe_version_string());
	ASSERT_EQ(QWE_EXIT_USAGE, qwe_not_implemented("test"));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(links_without_cli);
	GREATEST_MAIN_END();
}
