#include "greatest.h"
#include "src/kernel/redact.h"

#include <string.h>

/* Feeds the pieces in order, flushes, and returns the whole output. */
static void run(const char *const *pieces, size_t n, struct qwe_redact_buf *out)
{
	struct qwe_redactor r = {0};
	size_t i;

	for (i = 0; i < n; i++)
		qwe_redact_feed(&r, pieces[i], strlen(pieces[i]), out);
	qwe_redact_flush(&r, out);
	qwe_redactor_free(&r);
}

#define OUT_EQ(want, out)                                          \
	do {                                                       \
		ASSERT_EQ_FMT((size_t)strlen(want), (out).len, "%zu"); \
		ASSERT_MEM_EQ(want, (out).data, (out).len);            \
	} while (0)

TEST masks_a_secret(void)
{
	const char *p[] = {"token=hunter2 and hunter2 again\n"};
	struct qwe_redact_buf out = {0};

	qwe_redact_clear();
	qwe_redact_add("hunter2", 7);
	run(p, 1, &out);
	OUT_EQ("token=*** and *** again\n", out);
	qwe_redact_buf_free(&out);
	PASS();
}

TEST split_across_chunks(void)
{
	/* the secret arrives in every possible split, and in one byte at a time */
	const char *whole = "aa hunter2 bb";
	size_t cut;

	qwe_redact_clear();
	qwe_redact_add("hunter2", 7);
	for (cut = 1; cut < strlen(whole); cut++) {
		char a[32], b[32];
		const char *p[2] = {a, b};
		struct qwe_redact_buf out = {0};

		memcpy(a, whole, cut);
		a[cut] = '\0';
		memcpy(b, whole + cut, strlen(whole + cut) + 1); /* whole is 13 bytes, b is 32 */
		run(p, 2, &out);
		OUT_EQ("aa *** bb", out);
		qwe_redact_buf_free(&out);
	}
	{
		struct qwe_redactor r = {0};
		struct qwe_redact_buf out = {0};
		const char *s = whole;

		for (; *s; s++)
			qwe_redact_feed(&r, s, 1, &out);
		qwe_redact_flush(&r, &out);
		OUT_EQ("aa *** bb", out);
		qwe_redact_buf_free(&out);
		qwe_redactor_free(&r);
	}
	PASS();
}

TEST a_near_miss_is_not_held_forever(void)
{
	const char *p[] = {"hunt", "er3 done"};
	struct qwe_redact_buf out = {0};

	qwe_redact_clear();
	qwe_redact_add("hunter2", 7);
	run(p, 2, &out);
	OUT_EQ("hunter3 done", out);
	qwe_redact_buf_free(&out);
	PASS();
}

TEST held_tail_is_flushed_at_the_end(void)
{
	const char *p[] = {"ends with hunt"};
	struct qwe_redact_buf out = {0};

	qwe_redact_clear();
	qwe_redact_add("hunter2", 7);
	run(p, 1, &out);
	OUT_EQ("ends with hunt", out);
	qwe_redact_buf_free(&out);
	PASS();
}

TEST longest_secret_wins_and_new_ones_apply_later(void)
{
	struct qwe_redactor r = {0};
	struct qwe_redact_buf out = {0};

	qwe_redact_clear();
	qwe_redact_add("abc", 3);
	qwe_redact_add("abcdef", 6);
	qwe_redact_feed(&r, "abcdef abc late\n", 16, &out);
	qwe_redact_add("late", 4); /* a secret output that arrived meanwhile */
	qwe_redact_feed(&r, "late\n", 5, &out);
	qwe_redact_flush(&r, &out);
	OUT_EQ("*** *** late\n***\n", out);
	qwe_redact_buf_free(&out);
	qwe_redactor_free(&r);
	PASS();
}

TEST no_secrets_is_a_plain_copy(void)
{
	const char *p[] = {"nothing", " to hide"};
	struct qwe_redact_buf out = {0};

	qwe_redact_clear();
	run(p, 2, &out);
	OUT_EQ("nothing to hide", out);
	qwe_redact_buf_free(&out);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(masks_a_secret);
	RUN_TEST(split_across_chunks);
	RUN_TEST(a_near_miss_is_not_held_forever);
	RUN_TEST(held_tail_is_flushed_at_the_end);
	RUN_TEST(longest_secret_wins_and_new_ones_apply_later);
	RUN_TEST(no_secrets_is_a_plain_copy);
	GREATEST_MAIN_END();
}
