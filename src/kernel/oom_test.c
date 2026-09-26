/* qwe run under memory pressure: a two-job workflow, with the nth allocation
 * failed for every n, first in the engine and then in each forked step child.
 * Every run ends in one of two ways: it succeeds having done all of its work,
 * or it fails cleanly, with a message and a non-zero exit. A fault, a hang, or
 * a success with work missing fails the test. */
#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/kernel/put.h"
#include "src/kernel/oom_shim.h"
#include "src/kernel/qwe.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[512], childlog[600];
static int serial;

static void write_file(const char *path, const char *body)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		abort();
	qwe_out_str(fp, body);
	if (ferror(fp) || fclose(fp) != 0)
		abort(); /* the fixture was not fully written */
}

/* A fresh directory holding the workflow, whose steps leave a file each. */
static void fresh_case(char *dir, size_t cap)
{
	char path[700], body[1600];

	qwe_xfmt(dir, cap, "%s/case%d", root, ++serial);
	mkdir(dir, 0700);
	qwe_xfmt(body, sizeof body,
	    "jobs:\n"
	    "  build:\n    target: local\n    steps:\n      - run: echo built > %s/build.out\n"
	    "  test:\n    target: local\n    needs: [build]\n"
	    "    steps:\n      - run: test -f %s/build.out && echo tested > %s/test.out\n",
	    dir, dir, dir);
	qwe_xfmt(path, sizeof path, "%s/w.yaml", dir);
	write_file(path, body);
}

static int run_case(void *arg)
{
	char path[700];
	struct qwe_run_options opts = {0};

	qwe_xfmt(path, sizeof path, "%s/w.yaml", (const char *)arg);
	return qwe_run_workflow(path, &opts);
}

static int has_file(const char *dir, const char *name, const char *want)
{
	char path[700], got[64] = "";
	FILE *fp;

	qwe_xfmt(path, sizeof path, "%s/%s", dir, name);
	fp = fopen(path, "r");
	if (!fp)
		return 0;
	if (!fgets(got, sizeof got, fp))
		got[0] = 0;
	(void)fclose(fp); /* read-only */
	return strcmp(got, want) == 0;
}

static int all_work_done(const char *dir)
{
	return has_file(dir, "build.out", "built\n") && has_file(dir, "test.out", "tested\n");
}

/* The text of the case's one result.json, or "" if there is none. */
static void read_result(const char *dir, char *out, size_t cap)
{
	char path[1200];
	struct dirent *e;
	DIR *d;
	FILE *fp;
	size_t got = 0;

	out[0] = 0;
	qwe_xfmt(path, sizeof path, "%s/.qwe/runs", dir);
	d = opendir(path);
	if (!d)
		return;
	while ((e = readdir(d)) && e->d_name[0] == '.')
		;
	if (e) {
		qwe_xfmt(path, sizeof path, "%s/.qwe/runs/%s/result.json", dir, e->d_name);
		fp = fopen(path, "r");
		if (fp) {
			got = fread(out, 1, cap - 1, fp);
			(void)fclose(fp); /* read-only */
		}
	}
	closedir(d);
	out[got] = 0;
}

/* Runs a fresh case with the given injection; leaves the case dir in dir. */
static void inject(long n, long child_n, char *dir, size_t dircap, struct qwe_oom_outcome *o)
{
	fresh_case(dir, dircap);
	unlink(childlog);
	qwe_oom_child_log(childlog);
	if (qwe_oom_probe(n, child_n, run_case, dir, o) != 0)
		abort();
}

TEST injection_is_deterministic(void)
{
	struct qwe_oom_outcome a, b;
	char dir[600];
	long at;

	/* The count for a fixed scenario is the same every time, not just once. */
	inject(1L << 40, 0, dir, sizeof dir, &a);
	ASSERT(a.exited);
	ASSERT_EQ(0, a.code);
	ASSERT(a.count > 50);
	for (int i = 0; i < 3; i++) {
		inject(1L << 40, 0, dir, sizeof dir, &b);
		ASSERT_EQ_FMT(a.count, b.count, "%ld");
	}

	/* And so is the failing allocation: the same n ends the same way. */
	for (at = a.count / 4; at < a.count; at += a.count / 4) {
		struct qwe_oom_outcome x, y;
		char dx[600], dy[600];

		inject(at, 0, dx, sizeof dx, &x);
		inject(at, 0, dy, sizeof dy, &y);
		ASSERT_EQ_FMT(x.exited, y.exited, "%d");
		ASSERT_EQ_FMT(x.code, y.code, "%d");
		ASSERT_EQ_FMT(x.signal, y.signal, "%d");
		ASSERT_EQ_FMT(x.fired, y.fired, "%d");
		ASSERT_EQ_FMT(x.count, y.count, "%ld");
		ASSERT_EQ_FMT(all_work_done(dx), all_work_done(dy), "%d");
	}

	/* Same for the step child's allocations. */
	inject(0, 1L << 40, dir, sizeof dir, &a);
	ASSERT_EQ(0, a.code);
	for (long m = 1; m <= 3; m++) {
		char dx[600], dy[600];

		inject(0, m, dx, sizeof dx, &a);
		ASSERT_EQ_FMT(access(childlog, F_OK) == 0, 1, "%d");
		inject(0, m, dy, sizeof dy, &b);
		ASSERT_EQ_FMT(a.code, b.code, "%d");
		ASSERT_EQ_FMT(all_work_done(dx), all_work_done(dy), "%d");
	}
	PASS();
}

TEST run_survives_every_injection(void)
{
	struct qwe_oom_outcome base, o;
	char dir[600], result[8192];
	long at, m;
	int fired_children = 0;

	inject(0, 0, dir, sizeof dir, &base);
	ASSERT(base.exited);
	ASSERT_EQ_FMT(0, base.code, "%d");
	ASSERT(all_work_done(dir));

	/* Engine side: fail allocation 1, 2, 3 ... up to the last the run makes. */
	inject(1L << 40, 0, dir, sizeof dir, &o);
	ASSERT(o.count > 50);
	for (at = 1; at <= o.count; at++) {
		struct qwe_oom_outcome r;

		inject(at, 0, dir, sizeof dir, &r);
		if (!r.fired && r.exited) { /* an abort leaves no report, and had to fire to abort */
			qwe_diag("allocation %ld of %ld: never reached: exited %d code %d signal %d count %ld\n%s\n", at, o.count, r.exited, r.code, r.signal, r.count, r.err);
			FAIL();
		}
		if (r.exited && r.code == 0) {
			/* a success despite the failure has done all of its work */
			read_result(dir, result, sizeof result);
			if (!all_work_done(dir) || strstr(result, "failed") || strstr(result, "skipped")) {
				qwe_diag("allocation %ld: exit 0 with work missing:\n%s\n%s\n", at, r.err, result);
				FAIL();
			}
		} else if (r.exited && (r.code == QWE_EXIT_FAILED || r.code == QWE_EXIT_USAGE)) {
			if (!r.err[0]) {
				qwe_diag("allocation %ld: exit %d with no message\n", at, r.code);
				FAIL();
			}
		} else if (!r.exited && r.signal == SIGABRT && strstr(r.err, "out of memory")) {
			; /* allocation policy rule 2: nowhere to report, so name it and stop */
		} else {
			qwe_diag("allocation %ld of %ld: exited %d code %d signal %d\n%s\n",
			    at, o.count, r.exited, r.code, r.signal, r.err);
			FAIL();
		}
	}

	/* Step side: fail the child's 1st, 2nd ... allocation until none fails. */
	for (m = 1; m < 200; m++) {
		char res[8192];

		inject(0, m, dir, sizeof dir, &o);
		if (access(childlog, F_OK) != 0) {
			ASSERT_EQ_FMT(0, o.code, "%d");
			ASSERT(all_work_done(dir));
			break;
		}
		fired_children++;
		read_result(dir, res, sizeof res);
		/* the step failed, with a reason of the engine's (engine-error from the child, plugin-error from the
		 * backend that forked it) and not a bare exit code, and nothing hung */
		if (!o.exited || o.code != QWE_EXIT_FAILED || !(strstr(res, "\"reason\": \"engine-error\"") || strstr(res, "\"reason\": \"plugin-error\"")) ||
		    strstr(res, "\"reason\": \"exit-code\"")) {
			qwe_diag("child allocation %ld: exited %d code %d signal %d\n%s\n%s\n",
			    m, o.exited, o.code, o.signal, o.err, res);
			FAIL();
		}
	}
	ASSERT(m < 200);
	ASSERT(fired_children > 0);
	PASS();
}

/* A workflow past read_file's first 4 KiB (its buffer grows), run with --job. */
static void fresh_select_case(char *dir, size_t cap)
{
	char path[700];
	FILE *fp;
	int i;

	fresh_case(dir, cap);
	qwe_xfmt(path, sizeof path, "%s/w.yaml", dir);
	fp = fopen(path, "a");
	if (!fp)
		abort();
	for (i = 0; i < 80; i++)
		qwe_out_str(fp, "# padding, so that the file is longer than the reader's first buffer, and then some\n");
	if (ferror(fp) || fclose(fp) != 0)
		abort();
}

static int select_case(void *arg)
{
	static const char *const only[] = {"build"};
	struct qwe_run_options opts = {0};
	char path[700];

	opts.jobs = only;
	opts.njobs = 1;
	qwe_xfmt(path, sizeof path, "%s/w.yaml", (const char *)arg);
	return qwe_run_workflow(path, &opts);
}

TEST select_job_survives_every_injection(void)
{
	struct qwe_oom_outcome o, r;
	char dir[600];
	long at;

	fresh_select_case(dir, sizeof dir);
	ASSERT_EQ(0, qwe_oom_probe(1L << 40, 0, select_case, dir, &o));
	ASSERT_EQ_FMT(0, o.code, "%d");
	ASSERT(has_file(dir, "build.out", "built\n"));
	ASSERT(!has_file(dir, "test.out", "tested\n")); /* --job build: not what needs it */
	for (at = 1; at <= o.count; at++) {
		fresh_select_case(dir, sizeof dir);
		ASSERT_EQ(0, qwe_oom_probe(at, 0, select_case, dir, &r));
		if (!r.fired && r.exited) {
			qwe_diag("allocation %ld of %ld: never reached\n%s\n", at, o.count, r.err);
			FAIL();
		}
		if (r.exited && r.code == 0) {
			if (!has_file(dir, "build.out", "built\n") || has_file(dir, "test.out", "tested\n")) {
				qwe_diag("allocation %ld: exit 0 with the wrong work done\n%s\n", at, r.err);
				FAIL();
			}
		} else if ((r.exited && (r.code == QWE_EXIT_FAILED || r.code == QWE_EXIT_USAGE) && r.err[0]) ||
			   (!r.exited && r.signal == SIGABRT && strstr(r.err, "out of memory"))) {
			; /* a message and a non-zero exit, or allocation policy rule 2 */
		} else {
			qwe_diag("allocation %ld of %ld: exited %d code %d signal %d\n%s\n", at, o.count,
			    r.exited, r.code, r.signal, r.err);
			FAIL();
		}
	}
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	const char *tmp = getenv("TEST_TMPDIR");

	qwe_xfmt(root, sizeof root, "%s", tmp ? tmp : "/tmp");
	qwe_xfmt(childlog, sizeof childlog, "%s/child.log", root);
	/* the coverage flush allocates, which would move the injection points it is counting */
	unsetenv("QWE_LUA_COVERAGE");
	GREATEST_MAIN_BEGIN();
	RUN_TEST(injection_is_deterministic);
	RUN_TEST(run_survives_every_injection);
	RUN_TEST(select_job_survives_every_injection);
	GREATEST_MAIN_END();
}
