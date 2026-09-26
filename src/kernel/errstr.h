/* strerror, without the shared static buffer.
 *
 * strerror(3) may return a pointer into a buffer every caller shares, so
 * concurrency-mt-unsafe (docs/static-analysis.md) reports each call. This is
 * strerror_r into a buffer of the calling thread's own, and it never fails:
 * an errno strerror_r does not know reads "Unknown error <n>", as strerror's
 * does. The text is the C locale's, which is what the diagnostics and their
 * goldens are written in.
 *
 * The result is valid until the next call from the same thread. Use it in the
 * argument list of the message it belongs to, the way strerror(errno) was used. */
#ifndef QWE_KERNEL_ERRSTR_H
#define QWE_KERNEL_ERRSTR_H

const char *qwe_strerror(int err);

#endif
