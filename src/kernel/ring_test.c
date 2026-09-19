#include "greatest.h"
#include "src/kernel/ring.h"

#include <string.h>

TEST fifo_order(void)
{
	struct qwe_ring r;
	char out[8];

	ASSERT_EQ(0, qwe_ring_init(&r, 8));
	qwe_ring_write(&r, "abcde", 5);
	ASSERT_EQ(3, (int)qwe_ring_read(&r, out, 3));
	ASSERT_EQ(0, memcmp(out, "abc", 3));
	qwe_ring_write(&r, "fghi", 4); /* wraps */
	ASSERT_EQ(6, (int)qwe_ring_read(&r, out, sizeof out));
	ASSERT_EQ(0, memcmp(out, "defghi", 6));
	ASSERT_EQ(0UL, r.dropped);
	qwe_ring_free(&r);
	PASS();
}

TEST drop_accounting(void)
{
	struct qwe_ring r;
	char out[8];

	ASSERT_EQ(0, qwe_ring_init(&r, 4));
	qwe_ring_write(&r, "abcd", 4);
	ASSERT_EQ(0UL, r.dropped);

	/* Full: two more bytes push out the two oldest, and both are counted. */
	qwe_ring_write(&r, "ef", 2);
	ASSERT_EQ(2UL, r.dropped);
	ASSERT_EQ(4, (int)qwe_ring_read(&r, out, sizeof out));
	ASSERT_EQ(0, memcmp(out, "cdef", 4));

	/* A single write larger than the ring keeps its tail and counts the rest. */
	qwe_ring_write(&r, "0123456789", 10);
	ASSERT_EQ(2UL + 6UL, r.dropped);
	ASSERT_EQ(4, (int)qwe_ring_read(&r, out, sizeof out));
	ASSERT_EQ(0, memcmp(out, "6789", 4));

	qwe_ring_free(&r);
	PASS();
}

SUITE(ring)
{
	RUN_TEST(fifo_order);
	RUN_TEST(drop_accounting);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(ring);
	GREATEST_MAIN_END();
}
