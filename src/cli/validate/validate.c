#include "src/cli/validate/validate.h"
#include "src/kernel/qwe.h"

#include <stdio.h>
#include <string.h>

static int usage(void)
{
	fprintf(stderr, "usage: qwe validate <workflow.yaml> [-i <inventory.yaml>]\n");
	return QWE_EXIT_USAGE;
}

int qwe_cmd_validate(int argc, char **argv)
{
	const char *path = NULL, *inventory = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-i") == 0) {
			if (++i >= argc || inventory)
				return usage();
			inventory = argv[i];
		} else if (argv[i][0] == '-' || path) {
			return usage(); /* an unknown option, or a second file */
		} else {
			path = argv[i];
		}
	}
	if (!path)
		return usage();
	return qwe_validate_workflow(path, inventory);
}
