#include "src/kernel/qwe.h"
#include "src/kernel/put.h"

#include <stdio.h>

const char *qwe_version_string(void)
{
	return "qwe " QWE_VERSION;
}

int qwe_not_implemented(const char *cmd)
{
	qwe_diag("qwe %s: not implemented\n", cmd);
	return QWE_EXIT_USAGE;
}
