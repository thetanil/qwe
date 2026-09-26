/* setenv and unsetenv for tests, with the one place the analyzer is told why.
 *
 * Both rewrite the process environment, so with a second thread reading it
 * (getenv) they race, and concurrency-mt-unsafe reports each call. qwe has no
 * threads (docs/adr/0001-fork-per-plugin-step.md, and //src/cli:no_threads_test
 * fails if its binary can create one), and a test process is qwe's code plus
 * greatest, so the race cannot happen here. A test changes the environment
 * through these two, not through setenv directly, so that this is said once and
 * a stray setenv elsewhere is still a finding. */
#ifndef QWE_TESTING_ENV_H
#define QWE_TESTING_ENV_H

#include <stdlib.h>

static inline int qwe_test_setenv(const char *name, const char *value)
{
	// NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded, see the comment above
	return setenv(name, value, 1);
}

static inline int qwe_test_unsetenv(const char *name)
{
	// NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded, see the comment above
	return unsetenv(name);
}

#endif
