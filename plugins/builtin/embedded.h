/* Built-in plugin sources, compiled into the binary. */
#ifndef QWE_PLUGINS_BUILTIN_EMBEDDED_H
#define QWE_PLUGINS_BUILTIN_EMBEDDED_H

#include <stddef.h>

struct qwe_embedded {
	const char *name; /* the module name, as passed to require() */
	const unsigned char *data;
	size_t len;
};

/* Terminated by an entry whose name is NULL. */
extern const struct qwe_embedded qwe_embedded_modules[];

#endif
