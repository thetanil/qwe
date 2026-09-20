/* The file key source (qwe-ssh-sec II.3, II.6): the run key lives in
 * ~/.config/qwe/secret, 32 bytes raw or base64 of 32 bytes. A key file that
 * group or others can access is refused, as ssh refuses a private key. */
#ifndef QWE_SECRETS_KEYFILE_H
#define QWE_SECRETS_KEYFILE_H

#include "src/secrets/envelope.h"

#include <stddef.h>

/* $HOME/.config/qwe/secret into out. Returns 0, or -1 if HOME is unset. */
int qwe_key_default_path(char *out, size_t n);

/* Reads the key at path (the default path when NULL). Returns 0, or -1 with a
 * message in err that names the file. */
int qwe_key_load(const char *path, uint8_t key[QWE_KEY_BYTES], char *err, size_t err_size);

/* Creates the key file at path (the default path when NULL) with mode 0600 and
 * a new random key, and its directory (0700) if need be. Never overwrites: it
 * returns -1, with the reason in err, when the file exists. */
int qwe_key_generate(const char *path, char *err, size_t err_size);

#endif
