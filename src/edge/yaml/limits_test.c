#include "greatest.h"
#include "src/edge/yaml/transcode.h"
#include "src/testing/owned.h"

#include <stdlib.h>
#include <string.h>

TEST depth_limit(void)
{
	uint8_t *buf;
	size_t n;
	char err[128];
	/* One "[" too many, and nothing after them: if the whole document had to
	 * be parsed first, the error would be about the missing "]" instead. */
	char deep[QWE_YAML_MAX_DEPTH + 2];
	char ok[2 * QWE_YAML_MAX_DEPTH + 1];

	memset(deep, '[', QWE_YAML_MAX_DEPTH + 1);
	deep[QWE_YAML_MAX_DEPTH + 1] = '\0';
	ASSERT_EQ(-1, qwe_yaml_to_cbor(deep, strlen(deep), &buf, &n, NULL, err, sizeof err));
	ASSERT(strstr(err, "nesting too deep") != NULL);
	ASSERT_STR_EQ("1:65: nesting too deep", err);

	/* Exactly the limit is fine. */
	memset(ok, '[', QWE_YAML_MAX_DEPTH);
	memset(ok + QWE_YAML_MAX_DEPTH, ']', QWE_YAML_MAX_DEPTH);
	ok[2 * QWE_YAML_MAX_DEPTH] = '\0';
	ASSERT_EQ(0, qwe_yaml_to_cbor(ok, strlen(ok), &buf, &n, NULL, err, sizeof err));
	free(buf);
	PASS();
}

TEST size_limit(void)
{
	uint8_t *buf;
	size_t n;
	char err[128];
	size_t len = QWE_YAML_MAX_SIZE + 1;
	char *big = qwe_own(malloc(len + 1));

	ASSERT(big != NULL);
	memset(big, 'a', len);
	big[len] = '\0';
	ASSERT_EQ(-1, qwe_yaml_to_cbor(big, len, &buf, &n, NULL, err, sizeof err));
	ASSERT(strstr(err, "larger than") != NULL);

	/* At the limit it is accepted (one long plain scalar). */
	ASSERT_EQ(0, qwe_yaml_to_cbor(big, QWE_YAML_MAX_SIZE, &buf, &n, NULL, err, sizeof err));
	free(buf);
	PASS();
}

/* A key longer than the position tracker's first buffer, at two levels: its path
 * (the JSON Pointer used in error messages) outgrows the buffer more than once. */
TEST long_keys_grow_the_path(void)
{
	uint8_t *buf;
	size_t n;
	char err[128], doc[1500];
	char key[301];

	memset(key, 'k', sizeof key - 1);
	key[sizeof key - 1] = '\0';
	snprintf(doc, sizeof doc, "%s:\n  %s:\n    %s: 1\n", key, key, key);
	ASSERT_EQ(0, qwe_yaml_to_cbor(doc, strlen(doc), &buf, &n, NULL, err, sizeof err));
	free(buf);
	PASS();
}

SUITE(limits)
{
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(depth_limit);
	RUN_TEST(size_limit);
	RUN_TEST(long_keys_grow_the_path);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(limits);
	GREATEST_MAIN_END();
}
