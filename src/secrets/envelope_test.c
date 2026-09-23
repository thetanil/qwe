#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/secrets/envelope.h"
#include "src/secrets/keyfile.h"
#include "src/testing/owned.h"

#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint8_t key[QWE_KEY_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
					   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

TEST roundtrip(void)
{
	static const char plain[] = "hunter2 \n with \0 nul";
	char *env = qwe_envelope_seal(key, (const uint8_t *)plain, sizeof plain - 1);
	uint8_t *back;
	size_t n;
	const char *why;

	ASSERT(env != NULL);
	ASSERT(strncmp(env, "qwe:1:xchacha20poly1305:", 24) == 0);
	ASSERT(strstr(env, "hunter2") == NULL);
	ASSERT_EQ(0, qwe_envelope_check(env, &why));
	ASSERT_EQ(0, qwe_envelope_open(key, env, &back, &n, &why));
	ASSERT_EQ(sizeof plain - 1, n);
	ASSERT_MEM_EQ(plain, back, n);
	sodium_memzero(back, n);
	free(back);
	free(env);
	PASS();
}

TEST fresh_nonce_each_time(void)
{
	char *a = qwe_envelope_seal(key, (const uint8_t *)"x", 1), *b = qwe_envelope_seal(key, (const uint8_t *)"x", 1);

	ASSERT(strcmp(a, b) != 0);
	free(a);
	free(b);
	PASS();
}

TEST tamper_detected(void)
{
	char *env = qwe_own(qwe_envelope_seal(key, (const uint8_t *)"secret", 6)), *copy;
	uint8_t *back = NULL, other[QWE_KEY_BYTES];
	size_t n, i, len;
	const char *why = NULL;

	ASSERT(env != NULL);
	len = strlen(env);
	copy = qwe_own(malloc(len + 1));
	ASSERT(copy != NULL);
	/* every single-character change of the base64 part is refused */
	for (i = 24; i < len; i++) {
		memcpy(copy, env, len + 1);
		copy[i] = copy[i] == 'A' ? 'B' : 'A';
		ASSERT(qwe_envelope_open(key, copy, &back, &n, &why) != 0 || strcmp(copy, env) == 0);
	}
	/* the version and algorithm id are authenticated too */
	memcpy(copy, env, len + 1);
	copy[4] = '2';
	ASSERT(qwe_envelope_open(key, copy, &back, &n, &why) != 0);
	/* a different key */
	memcpy(other, key, sizeof other);
	other[0] ^= 1;
	ASSERT(qwe_envelope_open(other, env, &back, &n, &why) != 0);
	ASSERT(strstr(why, "authentication failed") != NULL);
	/* not an envelope at all */
	ASSERT(qwe_envelope_check("plain text", &why) != 0);
	ASSERT(qwe_envelope_check("qwe:1:xchacha20poly1305:AAAA", &why) != 0);
	PASS();
}

TEST key_file_perms(void)
{
	char dir[] = "/tmp/qwe-keytest-XXXXXX", path[256], err[256];
	uint8_t loaded[QWE_KEY_BYTES];
	struct stat st;

	ASSERT(mkdtemp(dir) != NULL);
	snprintf(path, sizeof path, "%s/a/b/secret", dir);
	ASSERT_EQ(0, qwe_key_generate(path, err, sizeof err));
	ASSERT_EQ(0, stat(path, &st));
	ASSERT_EQ(0600, st.st_mode & 0777);
	ASSERT_EQ(QWE_KEY_BYTES, st.st_size);
	ASSERT_EQ(0, qwe_key_load(path, loaded, err, sizeof err));
	/* a second keygen refuses */
	ASSERT(qwe_key_generate(path, err, sizeof err) != 0);
	ASSERT(strstr(err, "already exists") != NULL);
	/* group or world access is refused, naming the file and the mode */
	chmod(path, 0644);
	ASSERT(qwe_key_load(path, loaded, err, sizeof err) != 0);
	ASSERT(strstr(err, path) != NULL);
	ASSERT(strstr(err, "0644") != NULL);
	chmod(path, 0640);
	ASSERT(qwe_key_load(path, loaded, err, sizeof err) != 0);
	unlink(path);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	SET_TEARDOWN(qwe_release_owned, NULL);
	RUN_TEST(roundtrip);
	RUN_TEST(fresh_nonce_each_time);
	RUN_TEST(tamper_detected);
	RUN_TEST(key_file_perms);
	GREATEST_MAIN_END();
}
