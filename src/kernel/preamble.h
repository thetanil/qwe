/* The stdin preamble: how a step's declared env reaches its command without
 * touching argv (design §12, qwe-ssh-sec I.7). The command is started as
 *
 *     sh -c <qwe_preamble_bootstrap> sh <script>
 *
 * with the preamble followed by the command's own stdin as its stdin. The
 * bootstrap shell reads exactly the preamble, exports it, and execs the
 * script, which then reads the rest of stdin unchanged. Local and ssh
 * backends share it. */
#ifndef QWE_KERNEL_PREAMBLE_H
#define QWE_KERNEL_PREAMBLE_H

#include <stddef.h>

/* The script for `sh -c`. It takes the command's script as $1. */
extern const char qwe_preamble_bootstrap[];

/* Builds the preamble for n variables: one `NAME=value` line each (a backslash
 * is written as two, a newline as backslash-n) and an empty line to end it.
 * A name must match [A-Za-z_][A-Za-z0-9_]* and a value has no NUL byte.
 * Returns 0 and a malloc'd buffer, or -1 (nothing allocated) for a bad name or
 * value, with *bad set to the index of the offender. */
int qwe_preamble_build(const char *const *names, const char *const *values, size_t n, char **out, size_t *len,
		       size_t *bad);

#endif
