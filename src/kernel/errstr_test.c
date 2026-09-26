#include "greatest.h"
#include "src/kernel/errstr.h"

#include <errno.h>

TEST names_a_known_errno(void)
{
	ASSERT_STR_EQ("No such file or directory", qwe_strerror(ENOENT));
	ASSERT_STR_EQ("Permission denied", qwe_strerror(EACCES));
	ASSERT_STR_EQ("Cannot allocate memory", qwe_strerror(ENOMEM));
	PASS();
}

TEST numbers_an_unknown_errno(void)
{
	ASSERT_STR_EQ("Unknown error 99999", qwe_strerror(99999));
	PASS();
}

/* The caller reads errno for its next message after asking for this one's text. */
TEST leaves_errno_alone(void)
{
	errno = EPIPE;
	(void)qwe_strerror(99999);
	ASSERT_EQ(EPIPE, errno);
	PASS();
}

SUITE(errstr)
{
	RUN_TEST(names_a_known_errno);
	RUN_TEST(numbers_an_unknown_errno);
	RUN_TEST(leaves_errno_alone);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(errstr);
	GREATEST_MAIN_END();
}
