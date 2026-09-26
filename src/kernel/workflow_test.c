/* The forked child under memory pressure: an allocation that fails after fork
 * reaches the parent as a step result with a reason. */
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/kernel/put.h"
#include "src/kernel/qwe.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void *__real_calloc(size_t a, size_t b);

static pid_t parent_pid;

/* Every calloc fails in a forked step child; the engine's own are untouched. */
void *__wrap_calloc(size_t a, size_t b)
{
	return getpid() != parent_pid ? NULL : __real_calloc(a, b);
}

static char dir[512];

static void write_file(const char *name, const char *body)
{
	char path[600];
	FILE *fp;

	qwe_xfmt(path, sizeof path, "%s/%s", dir, name);
	fp = fopen(path, "w");
	if (!fp)
		abort();
	qwe_out_str(fp, body);
	if (ferror(fp) || fclose(fp) != 0)
		abort(); /* the fixture was not fully written */
}

/* The text of the one run's result.json. */
static int read_result(char *out, size_t cap)
{
	char path[1200];
	struct dirent *e;
	DIR *d;
	FILE *fp;
	size_t got;

	qwe_xfmt(path, sizeof path, "%s/.qwe/runs", dir);
	d = opendir(path);
	if (!d)
		return -1;
	while ((e = readdir(d)) && e->d_name[0] == '.')
		;
	if (!e) {
		closedir(d);
		return -1;
	}
	qwe_xfmt(path, sizeof path, "%s/.qwe/runs/%s/result.json", dir, e->d_name);
	closedir(d);
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	got = fread(out, 1, cap - 1, fp);
	out[got] = 0;
	(void)fclose(fp); /* read-only */
	return 0;
}

TEST child_oom_reports_a_reason(void)
{
	char path[600], result[4096];
	struct qwe_run_options opts = {0};
	int rc;

	write_file("w.yaml", "jobs:\n  j:\n    target: local\n    steps:\n      - run: echo hi\n");
	qwe_xfmt(path, sizeof path, "%s/w.yaml", dir);
	parent_pid = getpid();
	rc = qwe_run_workflow(path, &opts);

	ASSERT_EQ(QWE_EXIT_FAILED, rc);
	ASSERT_EQ(0, read_result(result, sizeof result));
	/* the step failed, and its reason is the engine's, not a bare exit code */
	ASSERT(strstr(result, "\"reason\": \"engine-error\""));
	ASSERT(!strstr(result, "\"reason\": \"exit-code\""));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	const char *tmp = getenv("TEST_TMPDIR");

	qwe_xfmt(dir, sizeof dir, "%s", tmp ? tmp : "/tmp");
	GREATEST_MAIN_BEGIN();
	RUN_TEST(child_oom_reports_a_reason);
	GREATEST_MAIN_END();
}
