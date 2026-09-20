#include "src/kernel/redact.h"

#include <stdlib.h>
#include <string.h>

#define MASK "***"

struct secret {
	char *value;
	size_t len;
};

static struct secret *set;
static size_t nset, capset;

void qwe_redact_add(const char *value, size_t len)
{
	size_t i;

	if (len == 0)
		return;
	for (i = 0; i < nset; i++)
		if (set[i].len == len && memcmp(set[i].value, value, len) == 0)
			return;
	if (nset == capset) {
		struct secret *grown = realloc(set, (capset ? capset * 2 : 16) * sizeof *set);

		if (!grown)
			return;
		set = grown;
		capset = capset ? capset * 2 : 16;
	}
	set[nset].value = malloc(len);
	if (!set[nset].value)
		return;
	memcpy(set[nset].value, value, len);
	set[nset].len = len;
	nset++;
}

void qwe_redact_clear(void)
{
	size_t i;

	for (i = 0; i < nset; i++) {
		volatile char *p = set[i].value;
		size_t k;

		for (k = 0; k < set[i].len; k++)
			p[k] = 0;
		free(set[i].value);
	}
	nset = 0;
}

void qwe_redact_buf_free(struct qwe_redact_buf *b)
{
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

static int buf_add(struct qwe_redact_buf *b, const char *src, size_t n)
{
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		char *grown;

		while (cap < b->len + n)
			cap *= 2;
		grown = realloc(b->data, cap);
		if (!grown)
			return -1;
		b->data = grown;
		b->cap = cap;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	return 0;
}

/* The length of the longest secret that starts exactly at p (n bytes left), or 0. */
static size_t match_at(const char *p, size_t n)
{
	size_t i, best = 0;

	for (i = 0; i < nset; i++)
		if (set[i].len <= n && set[i].len > best && memcmp(p, set[i].value, set[i].len) == 0)
			best = set[i].len;
	return best;
}

/* Whether the n bytes at p are a proper prefix of some secret. */
static int could_start(const char *p, size_t n)
{
	size_t i;

	for (i = 0; i < nset; i++)
		if (set[i].len > n && memcmp(p, set[i].value, n) == 0)
			return 1;
	return 0;
}

int qwe_redact_feed(struct qwe_redactor *r, const void *in, size_t n, struct qwe_redact_buf *out)
{
	size_t total = r->held_len + n, i = 0;
	char *buf;

	if (nset == 0 && r->held_len == 0)
		return buf_add(out, in, n);
	buf = malloc(total ? total : 1);
	if (!buf)
		return -1;
	if (r->held_len) /* held is NULL until something is held; memcpy from NULL is undefined */
		memcpy(buf, r->held, r->held_len);
	if (n)
		memcpy(buf + r->held_len, in, n);
	r->held_len = 0;
	while (i < total) {
		size_t m = match_at(buf + i, total - i);

		if (m) {
			if (buf_add(out, MASK, sizeof MASK - 1) < 0)
				goto fail;
			i += m;
			continue;
		}
		/* the rest might be the start of a secret that the next bytes finish */
		if (could_start(buf + i, total - i)) {
			if (total - i > r->held_cap) {
				char *grown = realloc(r->held, total - i);

				if (!grown)
					goto fail;
				r->held = grown;
				r->held_cap = total - i;
			}
			memcpy(r->held, buf + i, total - i);
			r->held_len = total - i;
			break;
		}
		if (buf_add(out, buf + i, 1) < 0)
			goto fail;
		i++;
	}
	free(buf);
	return 0;
fail:
	free(buf);
	return -1;
}

int qwe_redact_flush(struct qwe_redactor *r, struct qwe_redact_buf *out)
{
	int rc = r->held_len ? buf_add(out, r->held, r->held_len) : 0;

	r->held_len = 0;
	return rc;
}

void qwe_redactor_free(struct qwe_redactor *r)
{
	free(r->held);
	r->held = NULL;
	r->held_len = r->held_cap = 0;
}
