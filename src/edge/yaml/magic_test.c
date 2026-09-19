#include "greatest.h"
#include "src/edge/yaml/transcode.h"

#include <stdlib.h>
#include <string.h>

/* Transcodes y; on failure copies the message into err. */
static int try(const char *y, char *err)
{
	uint8_t *buf;
	size_t n;
	int rc = qwe_yaml_to_cbor(y, strlen(y), &buf, &n, NULL, err, 128);

	if (rc == 0)
		free(buf);
	return rc;
}

TEST anchor_rejected(void)
{
	char err[128];

	ASSERT_EQ(-1, try("a: 1\nb: &x 2\n", err));
	ASSERT_STR_EQ("2:4: anchors are not supported", err);
	ASSERT_EQ(-1, try("a: &m {k: 1}\n", err));
	ASSERT_STR_EQ("1:4: anchors are not supported", err);
	PASS();
}

TEST alias_rejected(void)
{
	char err[128];

	/* The anchor comes first in the document, so it is what gets rejected... */
	ASSERT_EQ(-1, try("a: &x 1\nb: *x\n", err));
	ASSERT_STR_EQ("1:4: anchors are not supported", err);
	/* ...and an alias to nothing is still rejected as an alias, not as a parse error. */
	ASSERT_EQ(-1, try("a: 1\nb: *x\n", err));
	ASSERT_STR_EQ("2:4: aliases are not supported", err);
	PASS();
}

TEST merge_key_rejected(void)
{
	char err[128];

	ASSERT_EQ(-1, try("a: 1\n<<: {b: 2}\n", err));
	ASSERT_STR_EQ("2:1: merge keys are not supported", err);
	/* Only the plain scalar is magic. A quoted "<<" is an ordinary key. */
	ASSERT_EQ(0, try("\"<<\": 1\n", err));
	PASS();
}

SUITE(magic)
{
	RUN_TEST(anchor_rejected);
	RUN_TEST(alias_rejected);
	RUN_TEST(merge_key_rejected);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(magic);
	GREATEST_MAIN_END();
}
