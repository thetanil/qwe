#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/put.h"
#include "src/testing/owned.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Reads a stream back from its start. A failure here is the test's own setup
 * failing, so it aborts. */
static char *read_back(FILE *fp)
{
	char *buf = calloc(1, 256);

	if (!buf || fflush(fp) != 0 || fseek(fp, 0, SEEK_SET) != 0)
		abort();
	if (fread(buf, 1, 255, fp) == 0 && ferror(fp))
		abort();
	return buf;
}

TEST out_writes_a_string_a_char_and_a_format(void)
{
	FILE *fp = qwe_own_file(tmpfile());
	char *got;

	ASSERT(fp != NULL);
	qwe_out_str(fp, "a=");
	qwe_out_ch(fp, '7');
	qwe_out_fmt(fp, " b=%s c=%d", "x", 3);
	ASSERT(!ferror(fp));
	got = qwe_own(read_back(fp));
	ASSERT_STR_EQ("a=7 b=x c=3", got);
	PASS();
}

/* A write that fails leaves the stream's error flag set, which is where the
 * caller looks (ferror before fclose): nothing is returned or thrown. */
TEST out_failure_is_left_in_the_error_flag(void)
{
	FILE *fp = qwe_own_file(fopen("/dev/full", "w"));

	ASSERT(fp != NULL);
	qwe_out_str(fp, "lost");
	(void)fflush(fp); /* the write is buffered; this is where /dev/full refuses it */
	ASSERT(ferror(fp));
	PASS();
}

/* qwe_diag writes exactly what fprintf(stderr, ...) did: it goes to fd 2. */
TEST diag_writes_to_stderr(void)
{
	FILE *cap = qwe_own_file(tmpfile());
	char *got;
	int saved;

	ASSERT(cap != NULL);
	saved = dup(2);
	ASSERT(saved >= 0);
	ASSERT(dup2(fileno(cap), 2) >= 0);
	qwe_diag("qwe: %s %d\n", "oops", 42);
	ASSERT(dup2(saved, 2) >= 0);
	close(saved);
	got = qwe_own(read_back(cap));
	ASSERT_STR_EQ("qwe: oops 42\n", got);
	PASS();
}

/* stderr closed: the write fails and there is no one to tell, so nothing
 * happens (no crash, no return value to ignore). */
TEST diag_survives_a_closed_stderr(void)
{
	int saved = dup(2);

	ASSERT(saved >= 0);
	close(2);
	qwe_diag("nobody hears this\n");
	ASSERT(dup2(saved, 2) >= 0);
	close(saved);
	PASS();
}

SUITE(put)
{
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(out_writes_a_string_a_char_and_a_format);
	RUN_TEST(out_failure_is_left_in_the_error_flag);
	RUN_TEST(diag_writes_to_stderr);
	RUN_TEST(diag_survives_a_closed_stderr);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	SET_TEARDOWN(qwe_release_owned, NULL);
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(put);
	GREATEST_MAIN_END();
}
