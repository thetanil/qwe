#include "alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *file, int line, size_t n)
{
	fprintf(stderr, "%s:%d: out of memory (%zu bytes)\n", file, line, n);
	abort();
}

void *qwe_xmalloc_at(size_t n, const char *file, int line)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		die(file, line, n);
	return p;
}

void *qwe_xcalloc_at(size_t n, size_t size, const char *file, int line)
{
	void *p = calloc(n ? n : 1, size ? size : 1);

	if (!p)
		die(file, line, n * size);
	return p;
}

void *qwe_xrealloc_at(void *p, size_t n, const char *file, int line)
{
	void *q = realloc(p, n ? n : 1);

	if (!q)
		die(file, line, n);
	return q;
}

char *qwe_xstrdup_at(const char *s, const char *file, int line)
{
	size_t n = strlen(s) + 1;
	char *p = qwe_xmalloc_at(n, file, line);

	return memcpy(p, s, n);
}
