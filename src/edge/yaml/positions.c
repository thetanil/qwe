#include "src/edge/yaml/positions.h"

#include <stdlib.h>
#include <string.h>

struct entry {
	char *pointer;
	struct qwe_pos key, value;
	int has_key, has_value;
};

struct qwe_positions {
	struct entry *e;
	size_t n, cap;
};

struct qwe_positions *qwe_positions_new(void)
{
	return calloc(1, sizeof(struct qwe_positions));
}

void qwe_positions_free(struct qwe_positions *p)
{
	size_t i;

	if (!p)
		return;
	for (i = 0; i < p->n; i++)
		free(p->e[i].pointer);
	free(p->e);
	free(p);
}

size_t qwe_positions_count(const struct qwe_positions *p)
{
	return p->n;
}

long qwe_positions_add(struct qwe_positions *p, const char *pointer, size_t len,
		       const struct qwe_pos *key, const struct qwe_pos *value)
{
	struct entry *e;

	if (p->n == p->cap) {
		size_t cap = p->cap ? p->cap * 2 : 64;
		struct entry *grown = realloc(p->e, cap * sizeof *grown);
		if (!grown)
			return -1;
		p->e = grown;
		p->cap = cap;
	}
	e = &p->e[p->n];
	memset(e, 0, sizeof *e);
	e->pointer = malloc(len + 1);
	if (!e->pointer)
		return -1;
	memcpy(e->pointer, pointer, len);
	e->pointer[len] = '\0';
	if (key) {
		e->key = *key;
		e->has_key = 1;
	}
	if (value) {
		e->value = *value;
		e->has_value = 1;
	}
	return (long)p->n++;
}

void qwe_positions_set_value(struct qwe_positions *p, long index, struct qwe_pos value)
{
	p->e[index].value = value;
	p->e[index].has_value = 1;
}

/* Orders by pointer, then by key position, so that of several entries with one
 * pointer the first in the source comes first. */
static int cmp(const void *a, const void *b)
{
	const struct entry *x = a, *y = b;
	int c = strcmp(x->pointer, y->pointer);

	if (c || !x->has_key || !y->has_key)
		return c;
	if (x->key.line != y->key.line)
		return x->key.line < y->key.line ? -1 : 1;
	if (x->key.col != y->key.col)
		return x->key.col < y->key.col ? -1 : 1;
	return 0;
}

void qwe_positions_duplicates(const struct qwe_positions *p, qwe_dup_fn fn, void *ud)
{
	size_t i, first = 0;

	for (i = 1; i < p->n; i++) {
		if (strcmp(p->e[i].pointer, p->e[first].pointer) != 0) {
			first = i;
			continue;
		}
		if (p->e[first].has_key && p->e[i].has_key)
			fn(p->e[i].pointer, p->e[first].key, p->e[i].key, ud);
	}
}

void qwe_positions_finish(struct qwe_positions *p)
{
	qsort(p->e, p->n, sizeof *p->e, cmp);
}

static const struct entry *find(const struct qwe_positions *p, const char *pointer)
{
	struct entry probe = {0};

	probe.pointer = (char *)pointer;
	return bsearch(&probe, p->e, p->n, sizeof *p->e, cmp);
}

int qwe_positions_value(const struct qwe_positions *p, const char *pointer, struct qwe_pos *out)
{
	const struct entry *e = find(p, pointer);

	if (!e || !e->has_value)
		return -1;
	*out = e->value;
	return 0;
}

int qwe_positions_key(const struct qwe_positions *p, const char *pointer, struct qwe_pos *out)
{
	const struct entry *e = find(p, pointer);

	if (!e || !e->has_key)
		return -1;
	*out = e->key;
	return 0;
}
