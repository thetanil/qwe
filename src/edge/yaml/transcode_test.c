#include "cbor.h"
#include "greatest.h"
#include "src/edge/yaml/transcode.h"

#include <stdlib.h>
#include <string.h>

static int to_cbor(const char *y, uint8_t **buf, size_t *n, char *err)
{
	return qwe_yaml_to_cbor(y, strlen(y), buf, n, err, 128);
}

/* Compares the transcoded bytes with an indefinite-length CBOR literal. */
TEST maps_seqs_and_scalars(void)
{
	static const uint8_t want[] = {
		0xbf,				 /* map(*) */
		0x61, 'a', 0x9f, 0x01, 0xf5, 0xf6, 0x61, 'x', 0xff, /* "a": [1, true, null, "x"] */
		0x61, 'k', 0x63, '1', '2', '3', /* "k": "123" (quoted below) */
		0xff};
	uint8_t *buf;
	size_t n;
	char err[128] = "";

	ASSERT_EQ(0, to_cbor("a: [1, true, ~, x]\nk: \"123\"\n", &buf, &n, err));
	ASSERT_EQ(sizeof want, n);
	ASSERT_EQ(0, memcmp(want, buf, n));
	free(buf);
	PASS();
}

TEST rejects_anchors_aliases_merge(void)
{
	uint8_t *buf;
	size_t n;
	char err[128] = "";

	ASSERT_EQ(-1, to_cbor("a: &x 1\n", &buf, &n, err));
	ASSERT_EQ(-1, to_cbor("a: &x 1\nb: *x\n", &buf, &n, err));
	ASSERT_EQ(-1, to_cbor("a: {<<: {b: 1}}\n", &buf, &n, err));
	PASS();
}

TEST reports_position(void)
{
	uint8_t *buf;
	size_t n;
	char err[128] = "";

	ASSERT_EQ(-1, to_cbor("a: [1, 2\nb: 3\n", &buf, &n, err));
	ASSERT(strchr(err, ':') != NULL);
	ASSERT(err[0] >= '1' && err[0] <= '9');
	PASS();
}

SUITE(transcode)
{
	RUN_TEST(maps_seqs_and_scalars);
	RUN_TEST(rejects_anchors_aliases_merge);
	RUN_TEST(reports_position);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(transcode);
	GREATEST_MAIN_END();
}
