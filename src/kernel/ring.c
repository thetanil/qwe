#include "src/kernel/ring.h"

#include <stdlib.h>
#include <string.h>

int qwe_ring_init(struct qwe_ring *r, size_t cap)
{
	memset(r, 0, sizeof *r);
	r->buf = malloc(cap);
	if (!r->buf)
		return -1;
	r->cap = cap;
	return 0;
}

void qwe_ring_free(struct qwe_ring *r)
{
	free(r->buf);
	memset(r, 0, sizeof *r);
}

void qwe_ring_write(struct qwe_ring *r, const void *data, size_t n)
{
	const unsigned char *p = data;
	size_t i;

	/* Only the last cap bytes can survive; the rest are dropped up front. */
	if (n > r->cap) {
		r->dropped += n - r->cap;
		p += n - r->cap;
		n = r->cap;
	}
	/* Make room by dropping the oldest bytes. */
	if (r->len + n > r->cap) {
		size_t over = r->len + n - r->cap;
		r->head = (r->head + over) % r->cap;
		r->len -= over;
		r->dropped += over;
	}
	for (i = 0; i < n; i++)
		r->buf[(r->head + r->len + i) % r->cap] = p[i];
	r->len += n;
}

size_t qwe_ring_read(struct qwe_ring *r, void *out, size_t n)
{
	unsigned char *o = out;
	size_t i;

	if (n > r->len)
		n = r->len;
	for (i = 0; i < n; i++)
		o[i] = r->buf[(r->head + i) % r->cap];
	r->head = (r->head + n) % r->cap;
	r->len -= n;
	return n;
}
