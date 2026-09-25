#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/secrets/keyfile.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

/* The key file and the ssh socket directory are held to one rule, and report a
 * breach in one voice: "<what> <path> has mode <mode>: it must not be ...". */
TEST mode_check_matches_socket_dir_check(void)
{
	char root[] = "/tmp/qwe-modetest-XXXXXX", file[128], dir[128], fe[256], de[256];
	struct stat fst, dst;
	int ff, df, ok;

	ASSERT(mkdtemp(root) != NULL);
	qwe_xfmt(file, sizeof file, "%s/secret", root);
	qwe_xfmt(dir, sizeof dir, "%s/sock", root);
	ASSERT_EQ(0, mkdir(dir, 0700));
	/* Modes are set and read through descriptors, not by name: nothing can swap the
	 * file between a chmod and the stat that checks it. */
	ff = open(file, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	df = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ok = ff >= 0 && df >= 0;

	/* private: both pass */
	ok = ok && fchmod(ff, 0600) == 0 && fchmod(df, 0700) == 0 && fstat(ff, &fst) == 0 && fstat(df, &dst) == 0;
	ok = ok && qwe_private_check(&fst, "the key file", file, fe, sizeof fe) == 0 &&
	     qwe_private_check(&dst, "the socket directory", dir, de, sizeof de) == 0;

	/* any group or other bit: both refuse, naming the path and the mode */
	ok = ok && fchmod(ff, 0640) == 0 && fchmod(df, 0750) == 0 && fstat(ff, &fst) == 0 && fstat(df, &dst) == 0;
	if (ff >= 0)
		(void)close(ff);
	if (df >= 0)
		(void)close(df);
	ASSERT(ok);
	ASSERT_EQ(-1, qwe_private_check(&fst, "the key file", file, fe, sizeof fe));
	ASSERT_EQ(-1, qwe_private_check(&dst, "the socket directory", dir, de, sizeof de));
	ASSERT(strstr(fe, "the key file ") == fe && strstr(fe, file) && strstr(fe, "has mode 0640"));
	ASSERT(strstr(de, "the socket directory ") == de && strstr(de, dir) && strstr(de, "has mode 0750"));
	ASSERT(strstr(fe, "it must not be accessible by group or others (chmod 600)") != NULL);
	ASSERT(strstr(de, "it must not be accessible by group or others (chmod 700)") != NULL);

	/* another owner: both refuse the same way */
	if (geteuid() == 0) {
		fst.st_uid = 1;
		dst.st_uid = 1;
		ASSERT_EQ(-1, qwe_private_check(&fst, "the key file", file, fe, sizeof fe));
		ASSERT_EQ(-1, qwe_private_check(&dst, "the socket directory", dir, de, sizeof de));
		ASSERT(strstr(fe, "is owned by uid 1") != NULL && strstr(de, "is owned by uid 1") != NULL);
	}

	unlink(file);
	rmdir(dir);
	rmdir(root);
	PASS();
}

/* Another owner is refused whoever runs the test: the stat is edited, not the file. */
TEST private_check_refuses_another_owner(void)
{
	struct stat st;
	char err[256];

	memset(&st, 0, sizeof st);
	st.st_mode = 0600;
	st.st_uid = geteuid() + 1;
	ASSERT_EQ(-1, qwe_private_check(&st, "the key file", "k", err, sizeof err));
	ASSERT(strstr(err, "is owned by uid") != NULL && strstr(err, "not by you") != NULL);
	PASS();
}

/* qwe_key_generate: a directory that cannot be made, a file that cannot be created,
 * a key that cannot be written; each says so, and none leaves a key file behind. */
TEST generate_failures_are_reported(void)
{
	char root[] = "/tmp/qwe-keygen-XXXXXX", path[512], blocker[256], ro[256], err[512];
	struct rlimit lim, old;
	struct sigaction ign, oldact;
	FILE *f;

	ASSERT(mkdtemp(root) != NULL);

	/* the parent "directory" is a file */
	qwe_xfmt(blocker, sizeof blocker, "%s/blocker", root);
	f = fopen(blocker, "w");
	ASSERT(f != NULL);
	ASSERT_EQ(0, fclose(f));
	qwe_xfmt(path, sizeof path, "%s/blocker/sub/secret", root);
	ASSERT_EQ(-1, qwe_key_generate(path, err, sizeof err));
	ASSERT(strstr(err, "cannot create ") != NULL);

	/* the directory exists but cannot be written to (skipped as root, who can) */
	qwe_xfmt(ro, sizeof ro, "%s/ro", root);
	ASSERT_EQ(0, mkdir(ro, 0500));
	qwe_xfmt(path, sizeof path, "%s/secret", ro);
	if (geteuid() != 0) {
		ASSERT_EQ(-1, qwe_key_generate(path, err, sizeof err));
		ASSERT(strstr(err, "cannot create the key file ") != NULL);
	}

	/* the write fails (the file size limit is zero): the half-made file is removed */
	qwe_xfmt(path, sizeof path, "%s/secret", root);
	memset(&ign, 0, sizeof ign);
	ign.sa_handler = SIG_IGN;
	sigaction(SIGXFSZ, &ign, &oldact);
	getrlimit(RLIMIT_FSIZE, &old);
	lim = old;
	lim.rlim_cur = 0;
	setrlimit(RLIMIT_FSIZE, &lim);
	ASSERT_EQ(-1, qwe_key_generate(path, err, sizeof err));
	setrlimit(RLIMIT_FSIZE, &old);
	sigaction(SIGXFSZ, &oldact, NULL);
	ASSERT(strstr(err, "cannot write ") != NULL);
	ASSERT_EQ(-1, access(path, F_OK));

	chmod(ro, 0700);
	rmdir(ro);
	unlink(blocker);
	rmdir(root);
	PASS();
}

/* A path longer than the directory buffer is refused before anything is made:
 * a truncated copy would have created the wrong directories. */
TEST generate_refuses_an_overlong_path(void)
{
	char root[] = "/tmp/qwe-keygen-long-XXXXXX", path[2048], first[64], err[512];
	size_t len;

	ASSERT(mkdtemp(root) != NULL);
	len = strlen(root);
	memcpy(path, root, len);
	while (len < 1200) {
		path[len++] = '/';
		path[len++] = 'd';
	}
	memcpy(path + len, "/secret", sizeof "/secret");
	ASSERT_EQ(-1, qwe_key_generate(path, err, sizeof err));
	ASSERT(strstr(err, "too long") != NULL);
	qwe_xfmt(first, sizeof first, "%s/d", root);
	ASSERT_EQ(-1, access(first, F_OK)); /* not one directory made */
	rmdir(root);
	PASS();
}

SUITE(keyfile)
{
	RUN_TEST(mode_check_matches_socket_dir_check);
	RUN_TEST(private_check_refuses_another_owner);
	RUN_TEST(generate_failures_are_reported);
	RUN_TEST(generate_refuses_an_overlong_path);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(keyfile);
	GREATEST_MAIN_END();
}
