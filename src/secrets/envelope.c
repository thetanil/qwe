#include "src/secrets/envelope.h"

#include <sodium.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX "qwe:1:xchacha20poly1305:"
#define NPUB crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define ABYTES crypto_aead_xchacha20poly1305_ietf_ABYTES
#define VARIANT sodium_base64_VARIANT_ORIGINAL

int qwe_secrets_init(void)
{
	return sodium_init() < 0 ? -1 : 0;
}

char *qwe_envelope_seal(const uint8_t key[QWE_KEY_BYTES], const uint8_t *plain, size_t n)
{
	size_t blob_len = NPUB + n + ABYTES, prefix_len = strlen(PREFIX);
	unsigned long long ct_len;
	uint8_t *blob;
	char *out;

	if (qwe_secrets_init() < 0)
		return NULL;
	blob = malloc(blob_len);
	out = malloc(prefix_len + sodium_base64_ENCODED_LEN(blob_len, VARIANT));
	if (!blob || !out) {
		free(blob);
		free(out);
		return NULL;
	}
	randombytes_buf(blob, NPUB);
	crypto_aead_xchacha20poly1305_ietf_encrypt(blob + NPUB, &ct_len, plain, n, (const uint8_t *)PREFIX,
						   prefix_len - 1, NULL, blob, key);
	memcpy(out, PREFIX, prefix_len);
	sodium_bin2base64(out + prefix_len, sodium_base64_ENCODED_LEN(blob_len, VARIANT), blob, blob_len, VARIANT);
	free(blob);
	return out;
}

/* Decodes the base64 part. Returns the blob (NPUB + ciphertext + tag) or NULL. */
static uint8_t *decode(const char *text, size_t *blob_len, const char **why)
{
	size_t prefix_len = strlen(PREFIX), cap;
	const char *b64;
	uint8_t *blob;

	if (strncmp(text, PREFIX, prefix_len) != 0) {
		*why = "not a qwe envelope (expected qwe:1:xchacha20poly1305:...)";
		return NULL;
	}
	b64 = text + prefix_len;
	cap = strlen(b64) / 4 * 3 + 3;
	blob = malloc(cap);
	if (!blob) {
		*why = "out of memory";
		return NULL;
	}
	if (sodium_base642bin(blob, cap, b64, strlen(b64), NULL, blob_len, NULL, VARIANT) != 0) {
		free(blob);
		*why = "the envelope is not valid base64";
		return NULL;
	}
	if (*blob_len < NPUB + ABYTES) {
		free(blob);
		*why = "the envelope is too short";
		return NULL;
	}
	return blob;
}

int qwe_envelope_check(const char *text, const char **why)
{
	size_t len;
	uint8_t *blob;

	if (qwe_secrets_init() < 0) {
		*why = "cannot initialize libsodium";
		return -1;
	}
	blob = decode(text, &len, why);
	if (!blob)
		return -1;
	free(blob);
	return 0;
}

int qwe_envelope_open(const uint8_t key[QWE_KEY_BYTES], const char *text, uint8_t **plain, size_t *n,
		      const char **why)
{
	size_t blob_len;
	unsigned long long plain_len;
	uint8_t *blob, *out;

	if (qwe_secrets_init() < 0) {
		*why = "cannot initialize libsodium";
		return -1;
	}
	blob = decode(text, &blob_len, why);
	if (!blob)
		return -1;
	out = malloc(blob_len - NPUB - ABYTES + 1);
	if (!out) {
		free(blob);
		*why = "out of memory";
		return -1;
	}
	if (crypto_aead_xchacha20poly1305_ietf_decrypt(out, &plain_len, NULL, blob + NPUB, blob_len - NPUB,
						       (const uint8_t *)PREFIX, strlen(PREFIX) - 1, blob, key) != 0) {
		sodium_memzero(out, blob_len - NPUB - ABYTES + 1);
		free(out);
		free(blob);
		*why = "authentication failed (wrong key, or the envelope was modified)";
		return -1;
	}
	out[plain_len] = '\0';
	free(blob);
	*plain = out;
	*n = (size_t)plain_len;
	return 0;
}
