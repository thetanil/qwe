/* snprintf, said three ways. Pick by what a cut-short result would mean.
 *
 * 1. A path, a key, a name: a truncated one is a wrong answer. Use qwe_fmt
 *    and fail when it returns -1.
 *
 * 2. A buffer sized to fit (allocated from the lengths going in, or a fixed
 *    buffer for a number), or a test fixture: truncation would be a bug here,
 *    not an input to handle. Use qwe_xfmt. It prints "file:line: formatted
 *    text truncated" to stderr and aborts, the way qwe_xmalloc does on OOM.
 *
 * 3. A diagnostic message for a person to read: a cut only makes it shorter.
 *    Use qwe_msg. It is snprintf that returns nothing, so nothing is ignored.
 *
 * All three NUL-terminate buf whenever size > 0.
 */
#ifndef QWE_KERNEL_FMT_H
#define QWE_KERNEL_FMT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static inline int qwe_vfmt(char *buf, size_t size, const char *fmt, va_list ap)
{
	int n = vsnprintf(buf, size, fmt, ap);

	return n < 0 || (size_t)n >= size ? -1 : 0;
}

/* 0 if all of it fit, -1 if it was cut short (or could not be formatted). */
static inline int qwe_fmt(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static inline int qwe_fmt(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = qwe_vfmt(buf, size, fmt, ap);
	va_end(ap);
	return rc;
}

static inline void qwe_xfmt_at(const char *file, int line, char *buf, size_t size, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));
static inline void qwe_xfmt_at(const char *file, int line, char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = qwe_vfmt(buf, size, fmt, ap);
	va_end(ap);
	if (rc < 0) {
		fprintf(stderr, "%s:%d: formatted text truncated (%lu bytes)\n", file, line, (unsigned long)size);
		abort();
	}
}

#define qwe_xfmt(buf, size, ...) qwe_xfmt_at(__FILE__, __LINE__, (buf), (size), __VA_ARGS__)

static inline void qwe_msg(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static inline void qwe_msg(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(buf, size, fmt, ap); /* a cut message is only shorter (see 3 above) */
	va_end(ap);
}

#endif
