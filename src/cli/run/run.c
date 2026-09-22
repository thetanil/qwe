#include "src/cli/run/run.h"
#include "src/kernel/qwe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
	fprintf(stderr,
		"usage: qwe run <workflow.yaml> [-i <inventory.yaml>] [--job <id>]... [--debug] "
		"[--summary <file>]\n");
	return QWE_EXIT_USAGE;
}

int qwe_cmd_run(int argc, char **argv)
{
	struct qwe_run_options opts = {0};
	const char *path = NULL;
	const char **jobs = calloc((size_t)argc, sizeof *jobs);
	size_t njobs = 0;
	int i, rc;

	if (!jobs) {
		fprintf(stderr, "qwe run: out of memory\n");
		return QWE_EXIT_USAGE;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--debug") == 0) {
			opts.debug = 1;
		} else if (strcmp(argv[i], "-i") == 0) {
			if (++i >= argc || opts.inventory) {
				free(jobs);
				return usage();
			}
			opts.inventory = argv[i];
		} else if (strcmp(argv[i], "--job") == 0) {
			if (++i >= argc) {
				free(jobs);
				return usage();
			}
			jobs[njobs++] = argv[i];
		} else if (strcmp(argv[i], "--summary") == 0) {
			if (++i >= argc || opts.summary) {
				free(jobs);
				return usage();
			}
			opts.summary = argv[i];
		} else if (argv[i][0] == '-' || path) {
			free(jobs);
			return usage(); /* an unknown option, or a second file */
		} else {
			path = argv[i];
		}
	}
	if (!path) {
		free(jobs);
		return usage();
	}
	opts.jobs = njobs ? jobs : NULL;
	opts.njobs = njobs;
	rc = qwe_run_workflow(path, &opts);
	free(jobs);
	return rc;
}
