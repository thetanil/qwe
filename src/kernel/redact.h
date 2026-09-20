/* Run-wide secret redaction, at the output tee (qwe-ssh-sec II.7).
 *
 * The run keeps one set of known plaintext values. Every byte a step writes
 * goes through a redactor on its way to the ring, so a value in the set never
 * reaches the log file, the terminal or any consumer: it becomes "***". A
 * value can straddle two reads, so the redactor holds back a tail that could
 * still turn out to be the start of one. */
#ifndef QWE_KERNEL_REDACT_H
#define QWE_KERNEL_REDACT_H

#include <stddef.h>

/* Adds a value to the run's set (a copy is kept). Empty values are ignored.
 * From now on, every redactor masks it. */
void qwe_redact_add(const char *value, size_t len);

/* Empties the set, zeroing what it held. */
void qwe_redact_clear(void);

/* A growable byte buffer for what a redactor produces. */
struct qwe_redact_buf {
	char *data;
	size_t len, cap;
};

void qwe_redact_buf_free(struct qwe_redact_buf *b);

/* One per stream. The held bytes are a proper prefix of some value. */
struct qwe_redactor {
	char *held;
	size_t held_len, held_cap;
};

/* Appends to out the redacted form of in[0..n), except for a tail that might
 * be the beginning of a secret: that stays held until more bytes arrive. */
int qwe_redact_feed(struct qwe_redactor *r, const void *in, size_t n, struct qwe_redact_buf *out);

/* The stream ended: appends the held bytes (they are not a whole secret). */
int qwe_redact_flush(struct qwe_redactor *r, struct qwe_redact_buf *out);

void qwe_redactor_free(struct qwe_redactor *r);

#endif
