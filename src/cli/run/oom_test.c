/* qwe run when its own argument list cannot be allocated (ticket quality/08):
 * a message and exit 2, not a fault. The workflow run itself is swept by
 * src/kernel:oom_test. */
#include "greatest.h"
#include "src/cli/run/run.h"
#include "src/kernel/oom_shim.h"
#include "src/kernel/qwe.h"

#include <string.h>

static int run_args(void *arg)
{
	char *argv[] = {"run", "--job", "build", "no-such-workflow.yaml", NULL};

	(void)arg;
	return qwe_cmd_run(4, argv);
}

TEST run_reports_a_failed_allocation(void)
{
	struct qwe_oom_outcome o;

	ASSERT_EQ(0, qwe_oom_probe(1, 0, run_args, NULL, &o));
	ASSERT(o.exited);
	ASSERT(o.fired);
	ASSERT_EQ_FMT(QWE_EXIT_USAGE, o.code, "%d");
	ASSERT(strstr(o.err, "qwe run: out of memory") != NULL);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(run_reports_a_failed_allocation);
	GREATEST_MAIN_END();
}
