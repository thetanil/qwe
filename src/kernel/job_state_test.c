#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/job_state.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define N 8

TEST legal_transitions(void)
{
	/* Exactly the edges of the design's §7.2, and nothing else. */
	static const int legal[N][N] = {
		/*             pend rdy run term succ fail skip canc */
		/* pending */ {0, 1, 0, 0, 0, 0, 1, 0},
		/* ready */   {0, 0, 1, 0, 0, 0, 0, 0},
		/* running */ {0, 0, 0, 1, 1, 1, 0, 0},
		/* terminating */ {0, 0, 0, 0, 0, 0, 0, 1},
		/* success */ {0, 0, 0, 0, 0, 0, 0, 0},
		/* failed */  {0, 0, 0, 0, 0, 0, 0, 0},
		/* skipped */ {0, 0, 0, 0, 0, 0, 0, 0},
		/* cancelled */ {0, 0, 0, 0, 0, 0, 0, 0},
	};
	int from, to;

	for (from = 0; from < N; from++)
		for (to = 0; to < N; to++)
			ASSERT_EQ_FMT(legal[from][to], qwe_job_transition_legal(from, to), "%d");

	/* And a walk along the happy path, and the skip and cancel paths. */
	{
		enum qwe_job_state s = QWE_JOB_PENDING;
		qwe_job_transition(&s, QWE_JOB_READY);
		qwe_job_transition(&s, QWE_JOB_RUNNING);
		qwe_job_transition(&s, QWE_JOB_SUCCESS);
		ASSERT(qwe_job_state_is_final(s));
		s = QWE_JOB_PENDING;
		qwe_job_transition(&s, QWE_JOB_SKIPPED);
		ASSERT_STR_EQ("skipped", qwe_job_state_name(s));
		s = QWE_JOB_RUNNING;
		qwe_job_transition(&s, QWE_JOB_TERMINATING);
		ASSERT(!qwe_job_state_is_final(s));
		qwe_job_transition(&s, QWE_JOB_CANCELLED);
		ASSERT_STR_EQ("cancelled", qwe_job_state_name(s));
	}
	PASS();
}

TEST illegal_transition_asserts(void)
{
	/* The illegal move aborts, so it runs in a child. */
	pid_t pid = fork();
	int st;

	ASSERT(pid >= 0);
	if (pid == 0) {
		enum qwe_job_state s = QWE_JOB_SUCCESS;
		close(2); /* keep the expected message out of the test log */
		qwe_job_transition(&s, QWE_JOB_RUNNING);
		_exit(0); /* not reached */
	}
	ASSERT_EQ(pid, waitpid(pid, &st, 0));
	ASSERT(WIFSIGNALED(st));
	ASSERT_EQ(SIGABRT, WTERMSIG(st));

	/* A skipped job cannot start either, and a pending job cannot jump to running. */
	ASSERT(!qwe_job_transition_legal(QWE_JOB_SKIPPED, QWE_JOB_RUNNING));
	ASSERT(!qwe_job_transition_legal(QWE_JOB_PENDING, QWE_JOB_RUNNING));
	PASS();
}

TEST exit_zero_with_skips(void)
{
	static const enum qwe_job_state ok[] = {QWE_JOB_SUCCESS, QWE_JOB_SKIPPED, QWE_JOB_SUCCESS};
	static const enum qwe_job_state has_failed[] = {QWE_JOB_SUCCESS, QWE_JOB_FAILED, QWE_JOB_SKIPPED};
	static const enum qwe_job_state has_cancelled[] = {QWE_JOB_SUCCESS, QWE_JOB_CANCELLED};

	/* Skipped is not a failure. (An e2e case cannot reach this: today a job is
	 * only skipped after a failed one, so the run fails anyway.) */
	ASSERT(qwe_jobs_all_ok(ok, 3));
	ASSERT(qwe_jobs_all_ok(NULL, 0));
	ASSERT(!qwe_jobs_all_ok(has_failed, 3));
	ASSERT(!qwe_jobs_all_ok(has_cancelled, 2));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(legal_transitions);
	RUN_TEST(illegal_transition_asserts);
	RUN_TEST(exit_zero_with_skips);
	GREATEST_MAIN_END();
}
