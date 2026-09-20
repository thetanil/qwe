#define _POSIX_C_SOURCE 200809L
#include "src/secrets/keyfile.h"

#include <errno.h>
#include <fcntl.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int qwe_key_default_path(char *out, size_t n)
{
	const char *home = getenv("HOME");

	if (!home || !*home)
		return -1;
	return snprintf(out, n, "%s/.config/qwe/secret", home) < (int)n ? 0 : -1;
}

static int resolve(const char *path, char *buf, size_t n, const char **out, char *err, size_t err_size)
{
	if (path) {
		*out = path;
		return 0;
	}
	if (qwe_key_default_path(buf, n) < 0) {
		snprintf(err, err_size, "no key file: HOME is not set");
		return -1;
	}
	*out = buf;
	return 0;
}

int qwe_private_check(const struct stat *st, const char *what, const char *path, char *err, size_t err_size)
{
	if (st->st_uid != geteuid()) {
		snprintf(err, err_size, "%s %s is owned by uid %u, not by you (uid %u)", what, path,
			 (unsigned)st->st_uid, (unsigned)geteuid());
		return -1;
	}
	if (st->st_mode & 077) {
		snprintf(err, err_size,
			 "%s %s has mode %04o: it must not be accessible by group or others (chmod %s)", what, path,
			 (unsigned)(st->st_mode & 0777), S_ISDIR(st->st_mode) ? "700" : "600");
		return -1;
	}
	return 0;
}

int qwe_key_load(const char *path, uint8_t key[QWE_KEY_BYTES], char *err, size_t err_size)
{
	char def[1024], raw[256];
	struct stat st;
	ssize_t got;
	int fd;

	if (resolve(path, def, sizeof def, &path, err, err_size) < 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		snprintf(err, err_size, "cannot read the key file %s: %s (make one with qwe keygen)", path,
			 strerror(errno));
		return -1;
	}
	/* the mode of the file we read, not of whatever the name points at now */
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
		snprintf(err, err_size, "the key file %s is not a regular file", path);
		close(fd);
		return -1;
	}
	if (qwe_private_check(&st, "the key file", path, err, err_size) < 0) {
		close(fd);
		return -1;
	}
	got = read(fd, raw, sizeof raw);
	close(fd);
	if (got == QWE_KEY_BYTES) {
		memcpy(key, raw, QWE_KEY_BYTES);
	} else {
		size_t bin_len = 0;

		while (got > 0 && (raw[got - 1] == '\n' || raw[got - 1] == '\r' || raw[got - 1] == ' '))
			got--;
		if (got <= 0 || sodium_base642bin(key, QWE_KEY_BYTES, raw, (size_t)got, NULL, &bin_len, NULL,
						  sodium_base64_VARIANT_ORIGINAL) != 0 ||
		    bin_len != QWE_KEY_BYTES) {
			snprintf(err, err_size, "the key file %s is neither 32 raw bytes nor base64 of 32 bytes", path);
			sodium_memzero(raw, sizeof raw);
			return -1;
		}
	}
	sodium_memzero(raw, sizeof raw);
	return 0;
}

int qwe_key_generate(const char *path, char *err, size_t err_size)
{
	char def[1024], dir[1024], *slash;
	uint8_t key[QWE_KEY_BYTES];
	size_t off = 0;
	int fd;

	if (resolve(path, def, sizeof def, &path, err, err_size) < 0)
		return -1;
	if (qwe_secrets_init() < 0) {
		snprintf(err, err_size, "cannot initialize libsodium");
		return -1;
	}
	snprintf(dir, sizeof dir, "%s", path);
	slash = strrchr(dir, '/');
	if (slash && slash != dir) {
		char *p;

		*slash = '\0';
		for (p = dir + 1; ; p++) {
			if (*p == '/' || *p == '\0') {
				char saved = *p;

				*p = '\0';
				if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
					snprintf(err, err_size, "cannot create %s: %s", dir, strerror(errno));
					return -1;
				}
				*p = saved;
				if (saved == '\0')
					break;
			}
		}
	}
	/* O_EXCL: an existing key is never replaced, which would orphan every secret sealed under it */
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) {
		snprintf(err, err_size, errno == EEXIST ? "the key file %s already exists: not overwriting it"
							 : "cannot create the key file %s: %s",
			 path, strerror(errno));
		return -1;
	}
	randombytes_buf(key, sizeof key);
	while (off < sizeof key) {
		ssize_t n = write(fd, key + off, sizeof key - off);

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			snprintf(err, err_size, "cannot write %s: %s", path, strerror(errno));
			sodium_memzero(key, sizeof key);
			close(fd);
			unlink(path);
			return -1;
		}
		off += (size_t)n;
	}
	sodium_memzero(key, sizeof key);
	close(fd);
	return 0;
}
