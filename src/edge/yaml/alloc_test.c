/* Fails one heap allocation at a time, and checks the transcoder reports
 * "out of memory" instead of retrying its way past the failure. Built with
 * -Wl,--wrap=malloc,realloc,calloc (see BUILD). */
#include "greatest.h"
#include "src/edge/yaml/transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t n);
void *__real_realloc(void *p, size_t n);
void *__real_calloc(size_t a, size_t b);

static int armed;
static long calls;   /* allocations seen while armed */
static long fail_at; /* which one returns NULL; 0 means none */

static int should_fail(void)
{
	return armed && ++calls == fail_at;
}

void *__wrap_malloc(size_t n)
{
	return should_fail() ? NULL : __real_malloc(n);
}

void *__wrap_realloc(void *p, size_t n)
{
	return should_fail() ? NULL : __real_realloc(p, n);
}

void *__wrap_calloc(size_t a, size_t b)
{
	return should_fail() ? NULL : __real_calloc(a, b);
}

static const char doc[] =
	"jobs:\n"
	"  build:\n"
	"    target: local\n"
	"    steps:\n"
	"      - run: echo hi\n"
	"      - {run: x, id: y}\n";

static int transcode(char *err, struct qwe_positions **pos)
{
	uint8_t *buf;
	size_t n;
	int rc = qwe_yaml_to_cbor(doc, strlen(doc), &buf, &n, pos, err, 128);

	if (rc == 0)
		free(buf);
	return rc;
}

TEST every_single_allocation_failure_is_fatal(void)
{
	char err[128];
	struct qwe_positions *pos = NULL;
	long total, k;

	/* Count the allocations of a clean run. */
	armed = 1;
	calls = 0;
	fail_at = 0;
	ASSERT_EQ(0, transcode(err, &pos));
	armed = 0;
	total = calls;
	qwe_positions_free(pos);
	printf("total allocations: %ld\n", total);
	ASSERT(total > 10); /* the fixture must actually exercise the position table */

	/* A failed allocation must fail the call. In particular it must not be
	 * mistaken for "output buffer too small" and retried into success. */
	for (k = 1; k <= total; k++) {
		pos = NULL;
		err[0] = '\0';
		calls = 0;
		fail_at = k;
		armed = 1;
		{
			int rc = transcode(err, &pos);
			armed = 0;
			if (rc == 0) {
				qwe_positions_free(pos);
				FAILm("allocation failure was retried past");
			}
		}
		ASSERT(pos == NULL);
		ASSERT(err[0] != '\0');
	}
	PASS();
}

SUITE(alloc)
{
	RUN_TEST(every_single_allocation_failure_is_fatal);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(alloc);
	GREATEST_MAIN_END();
}
