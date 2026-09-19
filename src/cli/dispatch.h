#ifndef QWE_CLI_DISPATCH_H
#define QWE_CLI_DISPATCH_H

typedef int (*qwe_cmd_fn)(int argc, char **argv);

struct qwe_subcommand {
	const char *name;
	qwe_cmd_fn fn;
};

/* NULL if name is not a subcommand. */
const struct qwe_subcommand *qwe_find_subcommand(const char *name);

/* argv[0] is the program name. Returns the process exit code. */
int qwe_dispatch(int argc, char **argv);

#endif
