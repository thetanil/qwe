#include "cbor.h"
#include "greatest.h"
#include "src/edge/yaml/transcode.h"

#include <stdlib.h>
#include <string.h>

TEST encrypted_tag_preserved(void)
{
	uint8_t *buf;
	size_t n;
	char err[128];
	CborParser parser;
	CborValue it, map;
	CborTag tag;
	char *text;
	size_t len;
	bool eq;

	ASSERT_EQ(0, qwe_yaml_to_cbor("tok: !encrypted \"v1:AbC+/=xyz\"\n", strlen("tok: !encrypted \"v1:AbC+/=xyz\"\n"), &buf, &n, NULL, err, sizeof err));
	ASSERT_EQ(CborNoError, cbor_parser_init(buf, n, 0, &parser, &it));
	ASSERT_EQ(CborNoError, cbor_value_enter_container(&it, &map));
	cbor_value_text_string_equals(&map, "tok", &eq);
	ASSERT(eq);
	cbor_value_advance(&map);
	ASSERT(cbor_value_is_tag(&map));
	ASSERT_EQ(CborNoError, cbor_value_get_tag(&map, &tag));
	ASSERT_EQ((unsigned)QWE_SECRET_TAG, (unsigned)tag);
	ASSERT_EQ(CborNoError, cbor_value_skip_tag(&map));
	ASSERT_EQ(CborNoError, cbor_value_dup_text_string(&map, &text, &len, &map));
	ASSERT_EQ(strlen("v1:AbC+/=xyz"), len);
	ASSERT_EQ(0, memcmp(text, "v1:AbC+/=xyz", len)); /* the envelope, unchanged */
	free(text);
	free(buf);
	PASS();
}

TEST unknown_tag_rejected(void)
{
	uint8_t *buf;
	size_t n;
	char err[128];
	static const char *const bad[] = {
		"a: 1\nb: !custom x\n",	/* unknown scalar tag */
		"a: !!str 5\n",		/* even a standard tag */
		"a: !encrypted [x]\n",	/* !encrypted only tags scalars */
		"!encrypted k: v\n",	/* and never a key */
	};
	static const char *const at[] = {"2:4:", "1:4:", "1:4:", "1:1:"};
	size_t i;

	for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		ASSERT_EQ(-1, qwe_yaml_to_cbor(bad[i], strlen(bad[i]), &buf, &n, NULL, err, sizeof err));
		ASSERT(strncmp(err, at[i], 4) == 0);
		ASSERT(strstr(err, "tag") != NULL);
	}
	PASS();
}

SUITE(tags)
{
	RUN_TEST(encrypted_tag_preserved);
	RUN_TEST(unknown_tag_rejected);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(tags);
	GREATEST_MAIN_END();
}
