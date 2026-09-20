/* Replays files through both fuzz entry points: the regression corpus (a
 * crash the fuzzer found stays fixed) and the e2e workflows and inventories
 * that seed it. Each argv entry is a file. */
#include "greatest.h"
#include "src/edge/yaml/fuzz_harness.h"

#include <stdio.h>
#include <stdlib.h>

static int nfiles;
static char **files;

static int replay(const char *path)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long n;

	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc(n ? (size_t)n : 1);
	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return -1;
	}
	fclose(f);
	qwe_fuzz_transcode(buf, (size_t)n);
	qwe_fuzz_chain(buf, (size_t)n);
	free(buf);
	return 0;
}

TEST replay_regression_corpus(void)
{
	int i;

	ASSERT_GT(nfiles, 0);
	for (i = 0; i < nfiles; i++) {
		if (replay(files[i]) != 0)
			FAILm(files[i]);
	}
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	nfiles = argc - 1;
	files = argv + 1;
	RUN_TEST(replay_regression_corpus);
	GREATEST_MAIN_END();
}
