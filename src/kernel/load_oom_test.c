/* The load path under memory pressure: every allocation on qwe validate's way
 * from file to verdict is failed in turn. Whatever fails, the answer is exit 2
 * and a message naming the file and memory, never a fault and never a verdict
 * reached by skipping the allocation. */
#include "greatest.h"
#include "src/kernel/qwe.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t a, size_t b);
void *__real_realloc(void *p, size_t n);
char *__real_strdup(const char *s);
char *__real_strndup(const char *s, size_t n);

/* The allocation numbered fail_at (1-based) returns NULL; 0 = never. */
static int fail_at, calls, fired;

static int should_fail(void)
{
	if (fail_at && ++calls == fail_at) {
		fired = 1;
		return 1;
	}
	return 0;
}

void *__wrap_malloc(size_t n) { return should_fail() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { return should_fail() ? NULL : __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { return should_fail() ? NULL : __real_realloc(p, n); }
char *__wrap_strdup(const char *s) { return should_fail() ? NULL : __real_strdup(s); }
char *__wrap_strndup(const char *s, size_t n) { return should_fail() ? NULL : __real_strndup(s, n); }

static char dir[512];

static void write_file(const char *name, const char *body)
{
	char path[600];
	FILE *fp;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	fp = fopen(path, "w");
	fputs(body, fp);
	fclose(fp);
}

/* Validates w.yaml (with an inventory), failing allocation number at; returns
 * the exit code and leaves what was written to stderr in err. */
static int validate_failing(int at, const char *workflow, char *err, size_t errcap)
{
	char path[600], errpath[600];
	int saved = dup(2), fd, rc;
	ssize_t got;

	snprintf(path, sizeof path, "%s/%s", dir, workflow);
	snprintf(errpath, sizeof errpath, "%s/stderr", dir);
	fd = open(errpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	dup2(fd, 2);
	close(fd);
	fail_at = at;
	calls = fired = 0;
	rc = qwe_validate_workflow(path, NULL);
	fail_at = 0;
	dup2(saved, 2);
	close(saved);
	fd = open(errpath, O_RDONLY);
	got = read(fd, err, errcap - 1);
	close(fd);
	err[got < 0 ? 0 : got] = 0;
	return rc;
}

/* Fails every allocation number in turn until one is past the last. */
static enum greatest_test_res sweep(const char *workflow, int expect_ok)
{
	char err[4096];
	int at, rc;

	rc = validate_failing(0, workflow, err, sizeof err);
	ASSERT_EQ_FMT(expect_ok ? 0 : 2, rc, "%d");
	for (at = 1; at < 5000; at++) {
		rc = validate_failing(at, workflow, err, sizeof err);
		if (!fired) {
			ASSERT_EQ_FMT(expect_ok ? 0 : 2, rc, "%d");
			break;
		}
		if (rc != 2 || !strstr(err, "memory") || !strstr(err, dir)) {
			fprintf(stderr, "allocation %d failed: exit %d, stderr:\n%s\n", at, rc, err);
			FAIL();
		}
	}
	ASSERT(at < 5000);
	PASS();
}

TEST valid_workflow_reports_every_allocation_failure(void)
{
	CHECK_CALL(sweep("w.yaml", 1));
	PASS();
}

TEST invalid_workflow_reports_every_allocation_failure(void)
{
	CHECK_CALL(sweep("bad.yaml", 0));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	const char *tmp = getenv("TEST_TMPDIR");

	snprintf(dir, sizeof dir, "%s", tmp ? tmp : "/tmp");
	write_file("w.yaml",
	    "jobs:\n"
	    "  build:\n    target: box\n    steps:\n      - run: echo hi\n"
	    "  test:\n    target: local\n    needs: [build]\n    steps:\n      - run: echo hi\n");
	write_file("inventory.yaml", "targets:\n  box:\n    backend: ssh\n    host: 192.0.2.1\n");
	write_file("bad.yaml",
	    "jobs:\n"
	    "  a:\n    needs: [b]\n    steps:\n      - run: echo hi\n"
	    "  b:\n    needs: [a]\n    steps:\n      - run: echo hi\n"
	    "  a:\n    steps: []\n");
	GREATEST_MAIN_BEGIN();
	RUN_TEST(valid_workflow_reports_every_allocation_failure);
	RUN_TEST(invalid_workflow_reports_every_allocation_failure);
	GREATEST_MAIN_END();
}
