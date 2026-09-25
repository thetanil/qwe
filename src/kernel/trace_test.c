#define _GNU_SOURCE
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/kernel/trace.h"
#include "src/testing/owned.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void path_for(char *path, size_t cap)
{
	const char *dir = getenv("TEST_TMPDIR");

	qwe_xfmt(path, cap, "%s/trace_test.%d", dir ? dir : "/tmp", (int)getpid());
}

/* A record longer than the writer's line buffer is cut short. The cut must
 * leave the file as one record per line: the next record starts on a new
 * line and the cut one still ends in a newline. */
TEST long_line_is_truncated(void)
{
	static char detail[2000];
	char path[300], l1[1024], l2[1024];
	struct qwe_trace t;
	FILE *fp;

	path_for(path, sizeof path);
	ASSERT_EQ(0, qwe_trace_open(&t, path, 0));
	memset(detail, 'd', sizeof detail - 1);
	qwe_trace_record(&t, "job", 0, QWE_LC_READY, "start", "step-running", "transition", NULL, detail);
	qwe_trace_record(&t, "job", 1, QWE_LC_READY, "next", "success", "transition", NULL, NULL);
	qwe_trace_close(&t);

	fp = qwe_own_file(fopen(path, "r"));
	ASSERT(fp != NULL);
	ASSERT(fgets(l1, sizeof l1, fp) != NULL);
	ASSERT(fgets(l2, sizeof l2, fp) != NULL);
	ASSERT_EQ(NULL, fgets(l1 + 0, 2, fp)); /* nothing else: no third line, no leftover */
	unlink(path);

	/* the long record was cut, and is still one whole line */
	ASSERT(strlen(l1) <= 512);
	ASSERT_EQ('\n', l1[strlen(l1) - 1]);
	ASSERT(strstr(l1, " job 0 ") != NULL);
	/* the record after it is intact, not glued to the cut one */
	ASSERT(strstr(l2, " job 1 ") != NULL);
	ASSERT(strstr(l2, "next") != NULL);
	ASSERT_EQ('\n', l2[strlen(l2) - 1]);
	PASS();
}

/* A trace that cannot be opened is an error; a closed one takes records and
 * drops them; a write that fails (a full device) is not an error of the step
 * being traced. */
TEST open_close_and_failed_writes(void)
{
	struct qwe_trace t;

	ASSERT_EQ(-1, qwe_trace_open(&t, "/nonexistent-dir/trace", 0));
	ASSERT_EQ(0, qwe_trace_open(&t, "/dev/full", 0));
	qwe_trace_record(&t, "job", 0, QWE_LC_READY, "start", NULL, "transition", NULL, NULL);
	qwe_trace_close(&t);
	qwe_trace_record(&t, "job", 0, QWE_LC_READY, "start", NULL, "transition", NULL, NULL);
	qwe_trace_close(&t);
	PASS();
}

/* An errno with no name is written as its number. */
TEST unknown_errno_is_its_number(void)
{
	ASSERT_STR_EQ("E99999", qwe_errno_name(99999));
	ASSERT_STR_EQ("ENOENT", qwe_errno_name(ENOENT));
	PASS();
}

SUITE(trace)
{
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(long_line_is_truncated);
	RUN_TEST(open_close_and_failed_writes);
	RUN_TEST(unknown_errno_is_its_number);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(trace);
	GREATEST_MAIN_END();
}
