/* Lua modules (and JSON schemas as Lua strings), compiled to bytecode into the binary. */
#ifndef QWE_KERNEL_LUA_EMBEDDED_H
#define QWE_KERNEL_LUA_EMBEDDED_H

#include <stddef.h>

struct qwe_embedded {
	const char *name; /* the module name, as passed to require() */
	const char *path; /* its source file, relative to the workspace */
	const unsigned char *data;
	size_t len;
};

/* Terminated by an entry whose name is NULL. */
extern const struct qwe_embedded qwe_embedded_modules[];

#endif
