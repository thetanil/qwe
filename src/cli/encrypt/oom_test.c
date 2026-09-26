#define _POSIX_C_SOURCE 200809L
/* qwe encrypt under memory pressure, for every allocation it makes (ticket
 * quality/08): the run either encrypts (and prints an envelope) or says why it
 * did not and exits non-zero. (The probe does not see stdout of a run that
 * succeeds: it is not flushed.) */
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/cli/encrypt/encrypt.h"
#include "src/kernel/oom_shim.h"
#include "src/kernel/qwe.h"
#include "src/kernel/put.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int encrypt_stdin(void *arg)
{
	char *argv[] = {"encrypt", NULL};

	(void)arg;
	return qwe_cmd_encrypt(1, argv);
}

TEST encrypt_survives_every_injection(void)
{
	struct qwe_oom_outcome o;
	long at, count;

	ASSERT_EQ(0, qwe_oom_probe(1L << 40, 0, encrypt_stdin, NULL, &o));
	ASSERT(o.exited);
	ASSERT_EQ_FMT(QWE_EXIT_OK, o.code, "%d");
	count = o.count;
	for (at = 1; at <= count; at++) {
		ASSERT_EQ(0, qwe_oom_probe(at, 0, encrypt_stdin, NULL, &o));
		if (!o.exited || o.code == QWE_EXIT_OK || !o.fired || !strstr(o.err, "qwe encrypt:")) {
			qwe_diag("allocation %ld of %ld: exited %d code %d signal %d fired %d\n%s\n", at, count,
			    o.exited, o.code, o.signal, o.fired, o.err);
			FAIL();
		}
	}
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	const char *tmp = getenv("TEST_TMPDIR");
	char path[600];
	FILE *fp;

	qwe_xfmt(path, sizeof path, "%s/home", tmp ? tmp : "/tmp");
	mkdir(path, 0700);
	setenv("HOME", path, 1);
	strncat(path, "/.config", sizeof path - strlen(path) - 1);
	mkdir(path, 0700);
	strncat(path, "/qwe", sizeof path - strlen(path) - 1);
	mkdir(path, 0700);
	strncat(path, "/secret", sizeof path - strlen(path) - 1);
	fp = fopen(path, "wb");
	if (!fp)
		abort();
	if (fwrite("0123456789abcdef0123456789abcdef", 1, 32, fp) != 32 || fclose(fp) != 0)
		abort();
	chmod(path, 0600);
	GREATEST_MAIN_BEGIN();
	RUN_TEST(encrypt_survives_every_injection);
	GREATEST_MAIN_END();
}
