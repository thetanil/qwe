#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/secrets/keyfile.h"

#include <stdio.h>
#include <string.h>
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
	fclose(f);
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

SUITE(keyfile)
{
	RUN_TEST(mode_check_matches_socket_dir_check);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(keyfile);
	GREATEST_MAIN_END();
}
