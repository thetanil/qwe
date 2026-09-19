#include "src/cli/run/run.h"
#include "src/kernel/qwe.h"

#include <stdio.h>

int qwe_cmd_run(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: qwe run <workflow.yaml>\n");
		return QWE_EXIT_USAGE;
	}
	return qwe_run_workflow(argv[1]);
}
