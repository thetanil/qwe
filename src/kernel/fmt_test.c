#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/fmt.h"

#include <string.h>

/* qwe_fmt says whether it all fit: an exact fit is 0, one byte over is -1,
 * and either way the buffer is a NUL-terminated prefix. */
TEST fmt_reports_truncation(void)
{
	char buf[6];

	ASSERT_EQ(0, qwe_fmt(buf, sizeof buf, "%s", "abcde"));
	ASSERT_STR_EQ("abcde", buf);
	ASSERT_EQ(-1, qwe_fmt(buf, sizeof buf, "%s/%s", "abc", "def"));
	ASSERT_STR_EQ("abc/d", buf);
	ASSERT_EQ(-1, qwe_fmt(buf, 0, "x"));
	PASS();
}

/* qwe_msg cuts a message short and still terminates it. */
TEST msg_cuts(void)
{
	char buf[4];

	qwe_msg(buf, sizeof buf, "error %d", 42);
	ASSERT_STR_EQ("err", buf);
	PASS();
}

/* qwe_xfmt passes a fit through. Its abort on a cut is the qwe_xmalloc
 * contract and is not forked here. */
TEST xfmt_fits(void)
{
	char buf[8];

	qwe_xfmt(buf, sizeof buf, "%d-%d", 12, 34);
	ASSERT_STR_EQ("12-34", buf);
	PASS();
}

SUITE(fmt)
{
	RUN_TEST(fmt_reports_truncation);
	RUN_TEST(msg_cuts);
	RUN_TEST(xfmt_fits);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(fmt);
	GREATEST_MAIN_END();
}
