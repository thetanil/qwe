/* Writing where a failed write is either checked once at the end or has nobody
 * to tell.
 *
 * 1. A data stream: qwe_out_str, qwe_out_ch and qwe_out_fmt.
 *
 * A stdio stream's error flag is sticky: once any write to it fails, ferror(fp)
 * stays true, and no later write clears it. So a writer of many small pieces
 * (result.json, the run summary, the bcembed table) need not test each one. It
 * writes freely through these three, which return nothing, and then does what
 * only it can do: check ferror(fp) before fclose(fp), and fclose's own result.
 * The one cast to (void) lives here, not at every call.
 *
 * Use them only where that check follows. A write whose failure has to be seen
 * at once (a length prefix, a size the next step depends on) checks its own
 * return.
 */
#ifndef QWE_KERNEL_PUT_H
#define QWE_KERNEL_PUT_H

#include <stdarg.h>
#include <stdio.h>

static inline void qwe_out_str(FILE *fp, const char *s)
{
	(void)fputs(s, fp); /* ferror(fp) is checked before fclose */
}

static inline void qwe_out_ch(FILE *fp, int c)
{
	(void)fputc(c, fp); /* ferror(fp) is checked before fclose */
}

static inline void qwe_out_fmt(FILE *fp, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static inline void qwe_out_fmt(FILE *fp, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vfprintf(fp, fmt, ap); /* ferror(fp) is checked before fclose */
	va_end(ap);
}

/* 2. A diagnostic to stderr: qwe_diag. A failed write to stderr has no one to
 * report to (stderr is where failures are reported), so ignoring it is the
 * right behaviour, and it is decided here, once, not at each of the calls.
 * Output is byte for byte what fprintf(stderr, ...) wrote. */
static inline void qwe_diag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void qwe_diag(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap); /* nowhere to report a failure of stderr */
	va_end(ap);
}

#endif
