#include "src/cli/run/run.h"
#include "src/kernel/qwe.h"

#include <stdio.h>
#include <string.h>

static int usage(void)
{
	fprintf(stderr, "usage: qwe run <workflow.yaml> [--debug]\n");
	return QWE_EXIT_USAGE;
}

int qwe_cmd_run(int argc, char **argv)
{
	struct qwe_run_options opts = {0};
	const char *path = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--debug") == 0)
			opts.debug = 1;
		else if (argv[i][0] == '-' || path)
			return usage(); /* an unknown option, or a second file */
		else
			path = argv[i];
	}
	if (!path)
		return usage();
	return qwe_run_workflow(path, &opts);
}
