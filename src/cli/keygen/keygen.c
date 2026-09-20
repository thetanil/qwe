#include "src/cli/keygen/keygen.h"
#include "src/kernel/qwe.h"
#include "src/secrets/keyfile.h"

#include <stdio.h>

int qwe_cmd_keygen(int argc, char **argv)
{
	char path[1024], err[512];

	(void)argv;
	if (argc != 1) {
		fprintf(stderr, "usage: qwe keygen\n");
		return QWE_EXIT_USAGE;
	}
	if (qwe_key_default_path(path, sizeof path) < 0) {
		fprintf(stderr, "qwe keygen: HOME is not set, so there is nowhere to put ~/.config/qwe/secret\n");
		return QWE_EXIT_USAGE;
	}
	if (qwe_key_generate(path, err, sizeof err) < 0) {
		fprintf(stderr, "qwe keygen: %s\n", err);
		return QWE_EXIT_USAGE;
	}
	fprintf(stderr, "qwe keygen: wrote a new key to %s (mode 0600)\n", path);
	return QWE_EXIT_OK;
}
