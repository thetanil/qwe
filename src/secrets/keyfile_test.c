#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/secrets/keyfile.h"

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
	FILE *f;

	ASSERT(mkdtemp(root) != NULL);
	snprintf(file, sizeof file, "%s/secret", root);
	snprintf(dir, sizeof dir, "%s/sock", root);
	f = fopen(file, "w");
	ASSERT(f != NULL);
	ASSERT_EQ(0, fclose(f));
	ASSERT_EQ(0, mkdir(dir, 0700));

	/* private: both pass */
	chmod(file, 0600);
	chmod(dir, 0700);
	ASSERT_EQ(0, stat(file, &fst));
	ASSERT_EQ(0, stat(dir, &dst));
	ASSERT_EQ(0, qwe_private_check(&fst, "the key file", file, fe, sizeof fe));
	ASSERT_EQ(0, qwe_private_check(&dst, "the socket directory", dir, de, sizeof de));

	/* any group or other bit: both refuse, naming the path and the mode */
	chmod(file, 0640);
	chmod(dir, 0750);
	ASSERT_EQ(0, stat(file, &fst));
	ASSERT_EQ(0, stat(dir, &dst));
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
	snprintf(blocker, sizeof blocker, "%s/blocker", root);
	f = fopen(blocker, "w");
	ASSERT(f != NULL);
	ASSERT_EQ(0, fclose(f));
	snprintf(path, sizeof path, "%s/blocker/sub/secret", root);
	ASSERT_EQ(-1, qwe_key_generate(path, err, sizeof err));
	ASSERT(strstr(err, "cannot create ") != NULL);

	/* the directory exists but cannot be written to (skipped as root, who can) */
	snprintf(ro, sizeof ro, "%s/ro", root);
	ASSERT_EQ(0, mkdir(ro, 0500));
	snprintf(path, sizeof path, "%s/secret", ro);
	if (geteuid() != 0) {
		ASSERT_EQ(-1, qwe_key_generate(path, err, sizeof err));
		ASSERT(strstr(err, "cannot create the key file ") != NULL);
	}

	/* the write fails (the file size limit is zero): the half-made file is removed */
	snprintf(path, sizeof path, "%s/secret", root);
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

SUITE(keyfile)
{
	RUN_TEST(mode_check_matches_socket_dir_check);
	RUN_TEST(private_check_refuses_another_owner);
	RUN_TEST(generate_failures_are_reported);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(keyfile);
	GREATEST_MAIN_END();
}
