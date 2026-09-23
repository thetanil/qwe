#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/edge/yaml/transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct qwe_positions *load(const char *y)
{
	uint8_t *buf;
	size_t n;
	char err[128];
	struct qwe_positions *pos = NULL;

	if (qwe_yaml_to_cbor(y, strlen(y), &buf, &n, &pos, err, sizeof err) < 0)
		return NULL;
	free(buf);
	return pos;
}

#define VALUE_AT(pos, ptr, l, c)                                          \
	do {                                                              \
		struct qwe_pos p_;                                        \
		ASSERT_EQ_FMT(0, qwe_positions_value(pos, ptr, &p_), "%d"); \
		ASSERT_EQ_FMT((unsigned)(l), p_.line, "%u");              \
		ASSERT_EQ_FMT((unsigned)(c), p_.col, "%u");               \
	} while (0)

#define KEY_AT(pos, ptr, l, c)                                            \
	do {                                                              \
		struct qwe_pos p_;                                        \
		ASSERT_EQ_FMT(0, qwe_positions_key(pos, ptr, &p_), "%d"); \
		ASSERT_EQ_FMT((unsigned)(l), p_.line, "%u");              \
		ASSERT_EQ_FMT((unsigned)(c), p_.col, "%u");               \
	} while (0)

TEST block_and_flow_positions(void)
{
	static const char doc[] =
		"jobs:\n"
		"  build:\n"
		"    target: local\n"
		"    steps:\n"
		"      - run: echo hi\n"
		"      - {run: x, id: y}\n";
	struct qwe_positions *pos = load(doc);

	ASSERT(pos != NULL);
	/* 1 root + jobs + build + target + steps + 2 items + run + run + id = 10 */
	ASSERT_EQ(10, (int)qwe_positions_count(pos));
	VALUE_AT(pos, "", 1, 1);
	VALUE_AT(pos, "/jobs", 2, 3);                 /* block map: its first key */
	VALUE_AT(pos, "/jobs/build/target", 3, 13);   /* scalar */
	VALUE_AT(pos, "/jobs/build/steps", 5, 7);     /* block sequence: its first "-" */
	VALUE_AT(pos, "/jobs/build/steps/0", 5, 9);   /* block map in a sequence */
	VALUE_AT(pos, "/jobs/build/steps/0/run", 5, 14);
	VALUE_AT(pos, "/jobs/build/steps/1", 6, 9);   /* flow map: its "{" */
	VALUE_AT(pos, "/jobs/build/steps/1/run", 6, 15);
	VALUE_AT(pos, "/jobs/build/steps/1/id", 6, 22);
	qwe_positions_free(pos);
	PASS();
}

TEST pointer_lookup_key_and_value(void)
{
	static const char doc[] =
		"target: local\n"
		"a/b: 1\n"
		"m~n:\n"
		"  k: [x, y]\n";
	struct qwe_positions *pos = load(doc);
	struct qwe_pos p;

	ASSERT(pos != NULL);
	/* A key and its value have different positions. */
	KEY_AT(pos, "/target", 1, 1);
	VALUE_AT(pos, "/target", 1, 9);
	/* "/" and "~" in a key are escaped as ~1 and ~0. */
	KEY_AT(pos, "/a~1b", 2, 1);
	VALUE_AT(pos, "/a~1b", 2, 6);
	KEY_AT(pos, "/m~0n", 3, 1);
	KEY_AT(pos, "/m~0n/k", 4, 3);
	VALUE_AT(pos, "/m~0n/k/1", 4, 10);
	/* Array items and the root have no key; unknown pointers are not found. */
	ASSERT_EQ(-1, qwe_positions_key(pos, "/m~0n/k/1", &p));
	ASSERT_EQ(-1, qwe_positions_key(pos, "", &p));
	ASSERT_EQ(-1, qwe_positions_value(pos, "/a/b", &p));
	ASSERT_EQ(-1, qwe_positions_value(pos, "/m~n", &p));
	qwe_positions_free(pos);
	PASS();
}

struct dups {
	int n;
	char pointer[4][32];
	struct qwe_pos first[4], second[4];
};

static void collect_dup(const char *pointer, struct qwe_pos first, struct qwe_pos second, void *ud)
{
	struct dups *d = ud;

	if (d->n < 4) {
		qwe_xfmt(d->pointer[d->n], sizeof d->pointer[0], "%s", pointer);
		d->first[d->n] = first;
		d->second[d->n] = second;
	}
	d->n++;
}

TEST duplicate_pointers_are_found(void)
{
	static const char doc[] =
		"jobs:\n"
		"  a: 1\n"
		"jobs:\n"
		"  b: 2\n"
		"steps:\n"
		"  - run: x\n"
		"  - run: x\n"
		"  - run: y\n"
		"    run: z\n";
	struct qwe_positions *pos = load(doc);
	struct dups d = {0};

	ASSERT(pos != NULL);
	qwe_positions_duplicates(pos, collect_dup, &d);
	/* /jobs and /steps/2/run; the repeated list entries are not duplicates. */
	ASSERT_EQ(2, d.n);
	ASSERT_STR_EQ("/jobs", d.pointer[0]);
	ASSERT_EQ(1, (int)d.first[0].line);
	ASSERT_EQ(3, (int)d.second[0].line);
	ASSERT_STR_EQ("/steps/2/run", d.pointer[1]);
	ASSERT_EQ(8, (int)d.first[1].line);
	ASSERT_EQ(9, (int)d.second[1].line);
	qwe_positions_free(pos);
	PASS();
}

SUITE(positions)
{
	RUN_TEST(duplicate_pointers_are_found);
	RUN_TEST(block_and_flow_positions);
	RUN_TEST(pointer_lookup_key_and_value);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(positions);
	GREATEST_MAIN_END();
}
