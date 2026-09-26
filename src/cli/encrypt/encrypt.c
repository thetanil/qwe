#include "src/cli/encrypt/encrypt.h"
#include "src/kernel/qwe.h"
#include "src/secrets/envelope.h"
#include "src/secrets/keyfile.h"
#include "src/kernel/put.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PLAIN ((size_t)64 * 1024)

/* Reads all of stdin into a malloc'd buffer. Returns its length, or -1. */
static ssize_t read_stdin(uint8_t **out)
{
	uint8_t *buf = malloc(MAX_PLAIN + 1);
	size_t n = 0;

	if (!buf)
		return -1;
	for (;;) {
		ssize_t got = read(0, buf + n, MAX_PLAIN + 1 - n);

		if (got < 0) {
			sodium_memzero(buf, MAX_PLAIN + 1);
			free(buf);
			return -1;
		}
		if (got == 0)
			break;
		n += (size_t)got;
		if (n > MAX_PLAIN) {
			sodium_memzero(buf, MAX_PLAIN + 1);
			free(buf);
			return -2;
		}
	}
	*out = buf;
	return (ssize_t)n;
}

/* The plaintext is read from stdin, never from an argument: an argument is in
 * the shell's history and in `ps` for every user. One trailing newline is not
 * part of the secret (echo adds it); use printf %s to be exact about the rest. */
int qwe_cmd_encrypt(int argc, char **argv)
{
	char err[512], *envelope;
	uint8_t key[QWE_KEY_BYTES], *plain = NULL;
	ssize_t n;

	(void)argv;
	if (argc != 1) {
		qwe_diag("qwe encrypt: takes no arguments: the plaintext is read from stdin, so it never\n"
				"appears in a command line (echo -n \"...\" | qwe encrypt)\n");
		return QWE_EXIT_USAGE;
	}
	if (qwe_secrets_init() < 0) {
		qwe_diag("qwe encrypt: cannot initialize libsodium\n");
		return QWE_EXIT_FAILED;
	}
	if (qwe_key_load(NULL, key, err, sizeof err) < 0) {
		qwe_diag("qwe encrypt: %s\n", err);
		return QWE_EXIT_USAGE;
	}
	n = read_stdin(&plain);
	if (n < 0) {
		if (n == -2)
			qwe_diag("qwe encrypt: the secret is longer than %lu bytes\n", (unsigned long)MAX_PLAIN);
		else
			qwe_diag("qwe encrypt: cannot read stdin\n");
		sodium_memzero(key, sizeof key);
		return QWE_EXIT_USAGE;
	}
	if (n > 0 && plain[n - 1] == '\n')
		n--;
	envelope = qwe_envelope_seal(key, plain, (size_t)n);
	sodium_memzero(plain, MAX_PLAIN + 1);
	sodium_memzero(key, sizeof key);
	free(plain);
	if (!envelope) {
		qwe_diag("qwe encrypt: encryption failed\n");
		return QWE_EXIT_FAILED;
	}
	puts(envelope);
	free(envelope);
	return QWE_EXIT_OK;
}
