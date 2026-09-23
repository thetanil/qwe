/* Test-owned memory and streams, released after every test.
 *
 * greatest's ASSERT and FAIL return out of a test on the first failure, past
 * any free() or fclose() further down. So a test does not free or close what it
 * allocates or opens itself. It hands it to qwe_own() or qwe_own_file() instead,
 * and qwe_release_owned, installed as greatest's teardown callback, frees and
 * closes all of it once the test is over, whether it passed or failed.
 *
 * Install it at the top of main (for RUN_TESTs outside any suite) and at the
 * top of every SUITE, since greatest clears the teardown when a suite ends:
 *
 *	SET_TEARDOWN(qwe_release_owned, NULL);
 *
 * Both take NULL and return it unchanged, so qwe_own(malloc(n)) needs no
 * separate null check before the ASSERT that the allocation worked. */
#ifndef QWE_TESTING_OWNED_H
#define QWE_TESTING_OWNED_H

#include <stdio.h>
#include <stdlib.h>

#define QWE_OWNED_MAX 32

static void *qwe_owned_mem[QWE_OWNED_MAX];
static FILE *qwe_owned_files[QWE_OWNED_MAX];
static size_t qwe_owned_nmem, qwe_owned_nfiles;

static inline void *qwe_own(void *p)
{
	if (qwe_owned_nmem == QWE_OWNED_MAX)
		abort();
	qwe_owned_mem[qwe_owned_nmem++] = p;
	return p;
}

static inline FILE *qwe_own_file(FILE *fp)
{
	if (qwe_owned_nfiles == QWE_OWNED_MAX)
		abort();
	qwe_owned_files[qwe_owned_nfiles++] = fp;
	return fp;
}

static inline void qwe_release_owned(void *unused)
{
	(void)unused;
	while (qwe_owned_nmem > 0)
		free(qwe_owned_mem[--qwe_owned_nmem]);
	while (qwe_owned_nfiles > 0) {
		FILE *fp = qwe_owned_files[--qwe_owned_nfiles];

		if (fp)
			fclose(fp);
	}
}

#endif
