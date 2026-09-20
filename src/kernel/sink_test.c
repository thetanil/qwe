#define _GNU_SOURCE
#include "greatest.h"
#include "src/kernel/sink.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char dir[256];

static void tmpdir(void)
{
	const char *base = getenv("TEST_TMPDIR");

	snprintf(dir, sizeof dir, "%s/sink_XXXXXX", base ? base : "/tmp");
	if (!mkdtemp(dir))
		dir[0] = '\0';
}

/* Reads a whole file into buf; returns its length or -1. */
static long slurp(const char *path, char *buf, size_t cap)
{
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, cap - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return (long)n;
}

/* Opens a sink over a scratch log dir and a scratch terminal file. */
static int open_sink(struct qwe_sink *s, char *termpath, size_t termcap)
{
	int term;

	tmpdir();
	if (!dir[0])
		return -1;
	snprintf(termpath, termcap, "%s/term", dir);
	term = open(termpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (term < 0)
		return -1;
	return qwe_sink_open(s, "job", dir, term);
}

TEST line_splitting(void)
{
	static char term_out[16384], log_out[16384], big[5001], expect[5100];
	char termpath[300], logpath[300];
	struct qwe_sink s;
	int term;

	ASSERT_EQ(0, open_sink(&s, termpath, sizeof termpath));
	term = s.term_fd;
	snprintf(logpath, sizeof logpath, "%s/job.log", dir);

	/* whole lines, one write; a line split across two writes */
	qwe_sink_write(&s, "one\ntwo\n", 8);
	qwe_sink_write(&s, "thr", 3);
	qwe_sink_write(&s, "ee\n", 3);
	/* a lone newline is an empty line, not nothing */
	qwe_sink_write(&s, "\n", 1);
	/* a line longer than the initial 256-byte buffer, and one that spans writes */
	memset(big, 'x', 5000);
	big[5000] = '\0';
	qwe_sink_write(&s, big, 2500);
	qwe_sink_write(&s, big + 2500, 2500);
	qwe_sink_write(&s, "\n", 1);
	/* no trailing newline: held until close, then flushed with one added */
	qwe_sink_write(&s, "tail", 4);
	qwe_sink_close(&s);
	close(term);

	snprintf(expect, sizeof expect, "[job] one\n[job] two\n[job] three\n[job] \n[job] %s\n[job] tail\n", big);
	ASSERT(slurp(termpath, term_out, sizeof term_out) >= 0);
	ASSERT_STR_EQ(expect, term_out);

	/* the log is the raw bytes: no prefix, no newline added to the tail */
	ASSERT(slurp(logpath, log_out, sizeof log_out) >= 0);
	snprintf(expect, sizeof expect, "one\ntwo\nthree\n\n%s\ntail", big);
	ASSERT_STR_EQ(expect, log_out);
	PASS();
}

TEST no_output_writes_nothing_to_the_terminal(void)
{
	static char term_out[64];
	char termpath[300];
	struct qwe_sink s;
	int term;

	ASSERT_EQ(0, open_sink(&s, termpath, sizeof termpath));
	term = s.term_fd;
	qwe_sink_close(&s);
	close(term);
	ASSERT_EQ(0L, slurp(termpath, term_out, sizeof term_out));
	PASS();
}

/* The log write fails (its descriptor is gone): the sink must not crash or
 * hang, and the terminal still gets its lines. The lost log bytes are silent
 * today; see the ticket comment. */
TEST failed_log_write_does_not_stop_the_terminal(void)
{
	static char term_out[64];
	char termpath[300];
	struct qwe_sink s;
	int term, real_log;

	ASSERT_EQ(0, open_sink(&s, termpath, sizeof termpath));
	term = s.term_fd;
	real_log = s.log_fd;
	s.log_fd = open("/dev/full", O_WRONLY); /* every write: ENOSPC */
	ASSERT(s.log_fd >= 0);
	qwe_sink_write(&s, "kept\n", 5);
	qwe_sink_close(&s);
	close(real_log);
	close(term);
	ASSERT(slurp(termpath, term_out, sizeof term_out) >= 0);
	ASSERT_STR_EQ("[job] kept\n", term_out);
	PASS();
}

TEST open_fails_when_the_directory_is_missing(void)
{
	struct qwe_sink s;

	ASSERT_EQ(-1, qwe_sink_open(&s, "job", "/nonexistent/dir", 2));
	PASS();
}

SUITE(sink)
{
	RUN_TEST(line_splitting);
	RUN_TEST(no_output_writes_nothing_to_the_terminal);
	RUN_TEST(failed_log_write_does_not_stop_the_terminal);
	RUN_TEST(open_fails_when_the_directory_is_missing);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(sink);
	GREATEST_MAIN_END();
}
