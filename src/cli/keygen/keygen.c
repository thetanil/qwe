#include "src/cli/keygen/keygen.h"
#include "src/kernel/qwe.h"
#include "src/secrets/keyfile.h"
#include "src/kernel/put.h"

#include <stdio.h>
#include <stdlib.h>

int qwe_cmd_keygen(int argc, char **argv)
{
	char path[1024], err[512];

	(void)argv;
	if (argc != 1) {
		qwe_diag("usage: qwe keygen\n");
		return QWE_EXIT_USAGE;
	}
	if (qwe_key_default_path(path, sizeof path) < 0) {
		const char *home = getenv("HOME");

		if (home && *home)
			qwe_diag("qwe keygen: HOME is too long for ~/.config/qwe/secret\n");
		else
			qwe_diag("qwe keygen: HOME is not set, so there is nowhere to put ~/.config/qwe/secret\n");
		return QWE_EXIT_USAGE;
	}
	if (qwe_key_generate(path, err, sizeof err) < 0) {
		qwe_diag("qwe keygen: %s\n", err);
		return QWE_EXIT_USAGE;
	}
	qwe_diag("qwe keygen: wrote a new key to %s (mode 0600)\n", path);
	return QWE_EXIT_OK;
}
