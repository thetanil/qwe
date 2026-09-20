/* Sealing and opening an envelope under memory pressure: the nth allocation
 * fails for every n. A failure must come back as NULL / -1 (open also says
 * "out of memory"), never as a fault, and a call that did not fail must have
 * done its work: a sealed envelope that opens back to the plaintext. */
#include "greatest.h"
#include "src/kernel/oom_shim.h"
#include "src/secrets/envelope.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t key[QWE_KEY_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
					   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const char plain[] = "hunter2";
static char sealed[256];

/* exit 0: it failed cleanly; 1: it worked; 2: it lied */
static int seal_case(void *arg)
{
	char *env = qwe_envelope_seal(key, (const uint8_t *)plain, sizeof plain - 1);

	(void)arg;
	if (!env)
		return 0;
	return strlen(env) == strlen(sealed) && strncmp(env, "qwe:1:xchacha20poly1305:", 24) == 0 ? 1 : 2;
}

static int open_case(void *arg)
{
	uint8_t *out = NULL;
	size_t n = 0;
	const char *why = NULL;

	(void)arg;
	if (qwe_envelope_open(key, sealed, &out, &n, &why) < 0)
		return why && strstr(why, "memory") ? 0 : 2;
	return n == sizeof plain - 1 && memcmp(out, plain, n) == 0 ? 1 : 2;
}

static enum greatest_test_res sweep(int (*fn)(void *))
{
	struct qwe_oom_outcome base, o;
	long at;

	ASSERT_EQ(0, qwe_oom_probe(1L << 40, 0, fn, NULL, &base)); /* armed past the end: it counts */
	ASSERT(base.exited);
	ASSERT_EQ_FMT(1, base.code, "%d");
	ASSERT(base.count > 0);
	for (at = 1; at <= base.count; at++) {
		ASSERT_EQ(0, qwe_oom_probe(at, 0, fn, NULL, &o));
		ASSERT(o.exited);
		ASSERT(o.fired);
		ASSERT_EQm("a failed allocation must fail the call", 0, o.code);
	}
	PASS();
}

TEST seal_survives_every_injection(void)
{
	return sweep(seal_case);
}

TEST open_survives_every_injection(void)
{
	return sweep(open_case);
}

SUITE(oom_suite)
{
	char *env = qwe_envelope_seal(key, (const uint8_t *)plain, sizeof plain - 1);

	snprintf(sealed, sizeof sealed, "%s", env);
	free(env);
	RUN_TEST(seal_survives_every_injection);
	RUN_TEST(open_survives_every_injection);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(oom_suite);
	GREATEST_MAIN_END();
}
