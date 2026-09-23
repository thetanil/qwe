#define _GNU_SOURCE
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/kernel/preamble.h"
#include "src/testing/owned.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Runs `sh -c bootstrap sh <script>` with input as its stdin (a pipe when
 * use_pipe, else a file) and collects stdout. Returns the byte count. */
static size_t run(const char *script, const char *input, size_t input_len, int use_pipe, char *out, size_t cap)
{
	int in[2], o[2];
	pid_t pid;
	size_t n = 0;
	ssize_t got;
	FILE *tmp;

	if (pipe(o) < 0)
		abort();
	if (use_pipe) {
		if (pipe(in) < 0)
			abort();
	} else {
		tmp = tmpfile();
		if (!tmp)
			abort();
		if (fwrite(input, 1, input_len, tmp) != input_len || fflush(tmp) != 0)
			abort();
		in[0] = dup(fileno(tmp));
		if (in[0] < 0)
			abort();
		lseek(in[0], 0, SEEK_SET);
		(void)fclose(tmp); /* flushed above; in[0] holds the file open */
	}
	pid = fork();
	if (pid == 0) {
		dup2(in[0], 0);
		dup2(o[1], 1);
		close(o[0]);
		close(o[1]);
		close(in[0]);
		if (use_pipe)
			close(in[1]); /* or cat never sees end of file */
		execlp("sh", "sh", "-c", qwe_preamble_bootstrap, "sh", script, (char *)NULL);
		_exit(127);
	}
	close(o[1]);
	close(in[0]);
	if (use_pipe) {
		/* small enough to fit the pipe: the test does not need a writer thread */
		if (write(in[1], input, input_len) < 0)
			abort();
		close(in[1]);
	}
	while ((got = read(o[0], out + n, cap - n)) > 0)
		n += (size_t)got;
	close(o[0]);
	waitpid(pid, NULL, 0);
	return n;
}

static char *with_preamble(const char *const *names, const char *const *values, size_t nvars, const char *data,
			   size_t data_len, size_t *total)
{
	char *pre, *all;
	size_t pre_len, bad;

	/* A setup failure aborts, like run()'s: returning NULL would hand run() a
	 * null input and an unset length. */
	if (qwe_preamble_build(names, values, nvars, &pre, &pre_len, &bad) < 0)
		abort();
	all = malloc(pre_len + data_len);
	if (!all)
		abort();
	memcpy(all, pre, pre_len);
	memcpy(all + pre_len, data, data_len);
	free(pre);
	*total = pre_len + data_len;
	return all;
}

TEST stdin_passthrough_exact(void)
{
	static const char data[] = {'a', '\0', 'b', '\n', '\n', (char)0xff, '\0', 'z'}; /* NULs, no trailing newline */
	const char *names[] = {"A", "B"};
	const char *values[] = {"one", "two\nlines\\n"};
	char out[64];
	size_t total, n;
	char *all = qwe_own(with_preamble(names, values, 2, data, sizeof data, &total));
	int use_pipe;

	for (use_pipe = 0; use_pipe <= 1; use_pipe++) {
		n = run("cat", all, total, use_pipe, out, sizeof out);
		ASSERT_EQ(sizeof data, n);
		ASSERT_MEM_EQ(data, out, sizeof data);
	}
	PASS();
}

TEST stdin_passthrough_empty_and_starting_with_newline(void)
{
	static const char data[] = "\n\nstarts with blank lines";
	char out[64];
	size_t total, n;
	char *all = qwe_own(with_preamble(NULL, NULL, 0, data, sizeof data - 1, &total));

	n = run("cat", all, total, 1, out, sizeof out);
	ASSERT_EQ(sizeof data - 1, n);
	ASSERT_MEM_EQ(data, out, n);
	all = qwe_own(with_preamble(NULL, NULL, 0, "", 0, &total));
	n = run("cat", all, total, 1, out, sizeof out);
	ASSERT_EQ(0, n);
	PASS();
}

TEST values_arrive_exactly(void)
{
	const char *names[] = {"PLAIN", "QUOTES", "MULTI", "TRAIL", "SLASH", "SUBST", "EMPTY"};
	const char *values[] = {"hello world", "it's \"q\"", "a\nb", "ends\n\n", "c:\\d\\n", "$(echo no) `x` $HOME", ""};
	char out[256];
	size_t total, n, i;
	char *all;

	for (i = 0; i < 7; i++) {
		char script[64];
		char want[64];

		qwe_xfmt(script, sizeof script, "printf %%s \"$%s\"", names[i]);
		all = qwe_own(with_preamble(names, values, 7, "", 0, &total));
		n = run(script, all, total, 1, out, sizeof out);
		qwe_xfmt(want, sizeof want, "%s", values[i]);
		ASSERT_EQ(strlen(want), n);
		ASSERT_MEM_EQ(want, out, n);
	}
	PASS();
}

TEST bad_names_are_refused(void)
{
	const char *names[] = {"OK", "not a name"};
	const char *values[] = {"x", "y"};
	char *out;
	size_t len, bad = 99;

	ASSERT_EQ(-1, qwe_preamble_build(names, values, 2, &out, &len, &bad));
	ASSERT_EQ(1, (int)bad);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(stdin_passthrough_exact);
	RUN_TEST(stdin_passthrough_empty_and_starting_with_newline);
	RUN_TEST(values_arrive_exactly);
	RUN_TEST(bad_names_are_refused);
	GREATEST_MAIN_END();
}
