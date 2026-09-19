/* A byte ring between a job's pipe reader and its log sink. When it is full,
 * a write overwrites the oldest bytes, and every overwritten byte is counted
 * in `dropped`. Nothing is ever lost without being counted. */
#ifndef QWE_KERNEL_RING_H
#define QWE_KERNEL_RING_H

#include <stddef.h>

#define QWE_RING_CAPACITY (1024 * 1024) /* spec: fixed at 1 MiB per job */

struct qwe_ring {
	unsigned char *buf;
	size_t cap;
	size_t head; /* index of the oldest byte */
	size_t len;  /* bytes currently held */
	unsigned long dropped;
};

/* Returns 0, or -1 if allocation fails. */
int qwe_ring_init(struct qwe_ring *r, size_t cap);
void qwe_ring_free(struct qwe_ring *r);

void qwe_ring_write(struct qwe_ring *r, const void *data, size_t n);

/* Moves up to n of the oldest bytes into out; returns how many. */
size_t qwe_ring_read(struct qwe_ring *r, void *out, size_t n);

#endif
