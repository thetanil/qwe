#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/summary.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

/* Runs qwe_summary_write into a fresh temp file and returns its content (caller frees). */
static char *written(const char *workflow_file, const char *overall_outcome, long run_duration_ms,
		     const struct qwe_job_result *jobs, size_t njobs)
{
	char path[] = "/tmp/qwe-summary-test-XXXXXX";
	int fd = mkstemp(path);
	FILE *fp;
	char *buf;
	long len;

	close(fd);
	qwe_summary_write(path, workflow_file, overall_outcome, run_duration_ms, jobs, njobs);
	fp = fopen(path, "r");
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	rewind(fp);
	buf = malloc((size_t)len + 1);
	fread(buf, 1, (size_t)len, fp);
	buf[len] = '\0';
	fclose(fp);
	unlink(path);
	return buf;
}

TEST heading_jobs_and_steps(void)
{
	struct qwe_step_result step = {
		.id = "s1", .outcome = "success", .changed = 1, .duration_ms = 12, .plugin = "run",
	};
	struct qwe_job_result job = {
		.id = "j", .outcome = "success", .duration_ms = 34, .steps = &step, .nsteps = 1,
	};
	char *out = written("w.yaml", "success", 100, &job, 1);

	ASSERT(strstr(out, "### w.yaml: \xE2\x9C\x85 success (100 ms)"));
	ASSERT(strstr(out, "| j | \xE2\x9C\x85 success |  | 34 |"));
	ASSERT(strstr(out, "#### j"));
	ASSERT(strstr(out, "| 1 | s1 | run | \xE2\x9C\x85 success | changed |  | 12 |"));
	free(out);
	PASS();
}

/* A skipped job (never started) has no steps table of its own. */
TEST skipped_job_has_no_steps_table(void)
{
	struct qwe_job_result job = {
		.id = "b", .outcome = "skipped", .reason = "dependency-failed", .duration_ms = -1, .nsteps = 0,
	};
	char *out = written("w.yaml", "failed", 5, &job, 1);

	ASSERT(strstr(out, "| b | \xE2\x8F\xAD\xEF\xB8\x8F skipped | dependency-failed |  |"));
	ASSERT_EQ(NULL, strstr(out, "#### b"));
	free(out);
	PASS();
}

/* success with changed false reports "unchanged"; a failed step's cell is blank. */
TEST changed_column(void)
{
	struct qwe_step_result steps[2] = {
		{.id = "a", .outcome = "success", .changed = 0, .duration_ms = 1},
		{.id = "b", .outcome = "failed", .reason = "exit-code", .duration_ms = 2},
	};
	struct qwe_job_result job = {
		.id = "j", .outcome = "failed", .duration_ms = 3, .steps = steps, .nsteps = 2,
	};
	char *out = written("w.yaml", "failed", 3, &job, 1);

	ASSERT(strstr(out, "| 1 | a | run | \xE2\x9C\x85 success | unchanged |  | 1 |"));
	ASSERT(strstr(out, "| 2 | b | run | \xE2\x9D\x8C failed |  | exit-code | 2 |"));
	free(out);
	PASS();
}

/* Two runs into the same file leave both reports, in order. */
TEST appends(void)
{
	char path[] = "/tmp/qwe-summary-test-XXXXXX";
	int fd = mkstemp(path);
	struct qwe_job_result job = {.id = "j", .outcome = "success", .duration_ms = 1};
	FILE *fp;
	char *buf;
	long len;
	char *first, *second;

	close(fd);
	ASSERT_EQ(0, qwe_summary_write(path, "one.yaml", "success", 1, &job, 1));
	ASSERT_EQ(0, qwe_summary_write(path, "two.yaml", "success", 1, &job, 1));
	fp = fopen(path, "r");
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	rewind(fp);
	buf = malloc((size_t)len + 1);
	fread(buf, 1, (size_t)len, fp);
	buf[len] = '\0';
	fclose(fp);
	unlink(path);
	first = strstr(buf, "one.yaml");
	second = strstr(buf, "two.yaml");
	ASSERT(first && second && first < second);
	free(buf);
	PASS();
}

TEST unwritable_path_fails(void)
{
	struct qwe_job_result job = {.id = "j", .outcome = "success"};

	ASSERT_EQ(-1, qwe_summary_write("/no/such/dir/summary.md", "w.yaml", "success", 0, &job, 1));
	PASS();
}

/* cancelled has its own marker; an outcome outside the vocabulary passes through as-is
 * (defensive: the engine only ever produces the four known outcomes). */
TEST every_outcome_marker(void)
{
	struct qwe_job_result cancelled_job = {.id = "j", .outcome = "cancelled", .duration_ms = -1};
	struct qwe_job_result weird_job = {.id = "j", .outcome = "weird", .duration_ms = -1};
	char *out;

	out = written("w.yaml", "cancelled", -1, &cancelled_job, 1);
	ASSERT(strstr(out, "\xE2\x8F\xB9\xEF\xB8\x8F cancelled"));
	free(out);

	out = written("w.yaml", "weird", -1, &weird_job, 1);
	ASSERT(strstr(out, "### w.yaml: weird ("));
	ASSERT(strstr(out, "| j | weird |  |  |"));
	free(out);
	PASS();
}

/* A write that fails after fopen (not just a failed open) still returns -1: RLIMIT_FSIZE
 * makes the buffered write fail at fclose. */
TEST write_failure_after_open_fails(void)
{
	char path[] = "/tmp/qwe-summary-test-XXXXXX";
	int fd = mkstemp(path);
	struct qwe_job_result job = {.id = "j", .outcome = "success"};
	struct rlimit old, tiny;
	int rc;

	close(fd);
	getrlimit(RLIMIT_FSIZE, &old);
	signal(SIGXFSZ, SIG_IGN);
	tiny.rlim_cur = 1;
	tiny.rlim_max = old.rlim_max;
	setrlimit(RLIMIT_FSIZE, &tiny);
	rc = qwe_summary_write(path, "w.yaml", "success", 0, &job, 1);
	setrlimit(RLIMIT_FSIZE, &old);
	unlink(path);
	ASSERT_EQ(-1, rc);
	PASS();
}

SUITE(summary)
{
	RUN_TEST(heading_jobs_and_steps);
	RUN_TEST(skipped_job_has_no_steps_table);
	RUN_TEST(changed_column);
	RUN_TEST(appends);
	RUN_TEST(unwritable_path_fails);
	RUN_TEST(every_outcome_marker);
	RUN_TEST(write_failure_after_open_fails);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(summary);
	GREATEST_MAIN_END();
}
