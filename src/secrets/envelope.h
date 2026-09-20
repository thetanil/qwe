/* The secret envelope: an authenticated, versioned encryption of one value
 * under the 32-byte run key (qwe-ssh-sec II.2, II.4).
 *
 * Text form, what `qwe encrypt` prints and `!encrypted "..."` holds:
 *
 *     qwe:1:xchacha20poly1305:<base64 of nonce || ciphertext || tag>
 *
 * The version and algorithm id are the AEAD's additional data, so changing
 * either is caught as tampering. There is no KDF: the key is used directly. */
#ifndef QWE_SECRETS_ENVELOPE_H
#define QWE_SECRETS_ENVELOPE_H

#include <stddef.h>
#include <stdint.h>

#define QWE_KEY_BYTES 32

/* Initializes libsodium. Returns 0, or -1. Safe to call more than once. */
int qwe_secrets_init(void);

/* Encrypts plain[0..n) under key with a random nonce. Returns a malloc'd
 * envelope string, or NULL. */
char *qwe_envelope_seal(const uint8_t key[QWE_KEY_BYTES], const uint8_t *plain, size_t n);

/* Checks that text is an envelope of the current version, without a key.
 * Returns 0, or -1 with *why pointing at a static message. */
int qwe_envelope_check(const char *text, const char **why);

/* Decrypts an envelope. Returns 0 with a malloc'd plaintext (n bytes, plus a
 * NUL after it) that the caller must sodium_memzero and free, or -1 with *why
 * set to a static message ("authentication failed" for a wrong key or a
 * modified envelope). */
int qwe_envelope_open(const uint8_t key[QWE_KEY_BYTES], const char *text, uint8_t **plain, size_t *n,
		      const char **why);

#endif
