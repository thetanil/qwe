#include "src/cli/validate/validate.h"
#include "src/kernel/qwe.h"

#include <stdio.h>

int qwe_cmd_validate(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: qwe validate <workflow.yaml>\n");
		return QWE_EXIT_USAGE;
	}
	return qwe_validate_workflow(argv[1]);
}
