#include "src/cli/dispatch.h"

int main(int argc, char **argv)
{
	/* adds const at both levels, which C does not do implicitly: the commands only read argv */
	return qwe_dispatch(argc, (const char *const *)argv);
}
