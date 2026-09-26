#include "src/cli/dispatch.h"

#include "src/cli/encrypt/encrypt.h"
#include "src/cli/keygen/keygen.h"
#include "src/cli/run/run.h"
#include "src/cli/serve/serve.h"
#include "src/cli/validate/validate.h"
#include "src/kernel/qwe.h"
#include "src/kernel/put.h"

#include <stdio.h>
#include <string.h>

static const struct qwe_subcommand subcommands[] = {
	{"run", qwe_cmd_run},
	{"validate", qwe_cmd_validate},
	{"encrypt", qwe_cmd_encrypt},
	{"keygen", qwe_cmd_keygen},
	{"serve", qwe_cmd_serve},
};

const struct qwe_subcommand *qwe_find_subcommand(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof subcommands / sizeof subcommands[0]; i++)
		if (strcmp(subcommands[i].name, name) == 0)
			return &subcommands[i];
	return NULL;
}

static int usage(void)
{
	qwe_diag("usage: qwe run <workflow.yaml> [-i <inventory.yaml>] [--job <id>]...\n"
	         "       qwe validate <workflow.yaml> [-i <inventory.yaml>]\n"
	         "       qwe encrypt\n"
	         "       qwe keygen\n"
	         "       qwe serve\n"
	         "       qwe --version\n");
	return QWE_EXIT_USAGE;
}

int qwe_dispatch(int argc, char **argv)
{
	const struct qwe_subcommand *cmd;

	if (argc < 2)
		return usage();
	if (strcmp(argv[1], "--version") == 0) {
		puts(qwe_version_string());
		return QWE_EXIT_OK;
	}
	cmd = qwe_find_subcommand(argv[1]);
	if (!cmd) {
		qwe_diag("qwe: unknown subcommand '%s'\n", argv[1]);
		return usage();
	}
	return cmd->fn(argc - 1, argv + 1);
}
