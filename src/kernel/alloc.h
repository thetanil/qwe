/* Allocation policy. Pick by where the failure can go, not by habit.
 *
 * 1. Where a failure can be reported, report it. Load, validate and job-setup
 *    code has a caller that can print a message and exit 2: use plain
 *    malloc/calloc/realloc/strdup, check the result, and return an error.
 *
 * 2. Where it cannot, say so and abort. The output path (a log line the sink
 *    cannot store) has nowhere to send a failure, and the log is the source of
 *    truth: losing a line silently is worse than stopping. Use qwe_xmalloc and
 *    friends. They print "file:line: out of memory (N bytes)" to stderr and
 *    abort(), which names the allocation instead of faulting on NULL.
 *
 * 3. The child after fork is its own case. An allocation failure there must
 *    reach the parent as a step result with a reason, not as a dead child:
 *    check, and report over the result pipe (see qwe_result). Do not use the
 *    aborting helpers there.
 *
 * Outside alloc.c, a bare allocation call is either checked at its call site
 * or is one of the helpers below.
 */
#ifndef QWE_KERNEL_ALLOC_H
#define QWE_KERNEL_ALLOC_H

#include <stddef.h>

void *qwe_xmalloc_at(size_t n, const char *file, int line);
void *qwe_xcalloc_at(size_t n, size_t size, const char *file, int line);
void *qwe_xrealloc_at(void *p, size_t n, const char *file, int line);
char *qwe_xstrdup_at(const char *s, const char *file, int line);

#define qwe_xmalloc(n) qwe_xmalloc_at((n), __FILE__, __LINE__)
#define qwe_xcalloc(n, size) qwe_xcalloc_at((n), (size), __FILE__, __LINE__)
#define qwe_xrealloc(p, n) qwe_xrealloc_at((p), (n), __FILE__, __LINE__)
#define qwe_xstrdup(s) qwe_xstrdup_at((s), __FILE__, __LINE__)

#endif
