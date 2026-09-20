#define _POSIX_C_SOURCE 200809L /* struct timespec, which trace.h needs, under -std=c99 */
#include "greatest.h"
#include "src/kernel/trace.h"

#include <errno.h>
#include <string.h>

/* Built with QWE_NO_STRERRORNAME_NP and strerrorname_np renamed to nothing, as
 * on a libc without it (musl, glibc < 2.32): the names must still come out. */
TEST names_a_known_errno(void)
{
	ASSERT_STR_EQ("EAGAIN", qwe_errno_name(EAGAIN));
	ASSERT_STR_EQ("ENOENT", qwe_errno_name(ENOENT));
	ASSERT_STR_EQ("EACCES", qwe_errno_name(EACCES));
	ASSERT_STR_EQ("ENOMEM", qwe_errno_name(ENOMEM));
	PASS();
}

TEST numbers_an_unknown_errno(void)
{
	ASSERT_STR_EQ("E99999", qwe_errno_name(99999));
	PASS();
}

SUITE(errno_name)
{
	RUN_TEST(names_a_known_errno);
	RUN_TEST(numbers_an_unknown_errno);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(errno_name);
	GREATEST_MAIN_END();
}
