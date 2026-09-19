/* Round-trips the value shapes qwe relies on through TinyCBOR. */
#include "cbor.h"
#include "greatest.h"

#include <string.h>

/* Placeholder tag number for a secret value. Ticket 15 picks the real one
 * (RFC 8949 first-come-first-served range, >= 32768) in a shared header. */
#define TEST_SECRET_TAG 32768u

/* {"name": "qwe", "args": ["a", "b"], "blob": h'0001ff', "pw": 32768("hunter2")} */
static size_t encode(uint8_t *buf, size_t cap)
{
	static const uint8_t blob[] = {0x00, 0x01, 0xff};
	CborEncoder root, map, arr;
	cbor_encoder_init(&root, buf, cap, 0);
	cbor_encoder_create_map(&root, &map, 4);
	cbor_encode_text_stringz(&map, "name");
	cbor_encode_text_stringz(&map, "qwe");
	cbor_encode_text_stringz(&map, "args");
	cbor_encoder_create_array(&map, &arr, 2);
	cbor_encode_text_stringz(&arr, "a");
	cbor_encode_text_stringz(&arr, "b");
	cbor_encoder_close_container(&map, &arr);
	cbor_encode_text_stringz(&map, "blob");
	cbor_encode_byte_string(&map, blob, sizeof blob);
	cbor_encode_text_stringz(&map, "pw");
	cbor_encode_tag(&map, TEST_SECRET_TAG);
	cbor_encode_text_stringz(&map, "hunter2");
	cbor_encoder_close_container(&root, &map);
	return cbor_encoder_get_buffer_size(&root, buf);
}

static int key_is(CborValue *v, const char *want)
{
	bool eq = false;
	cbor_value_text_string_equals(v, want, &eq);
	return eq;
}

TEST tagged_value_survives(void)
{
	uint8_t buf[128];
	size_t n = encode(buf, sizeof buf);
	CborParser parser;
	CborValue it, map, arr;

	ASSERT(n > 0);
	ASSERT_EQ(CborNoError, cbor_parser_init(buf, n, 0, &parser, &it));
	ASSERT(cbor_value_is_map(&it));
	ASSERT_EQ(CborNoError, cbor_value_enter_container(&it, &map));

	/* map */
	ASSERT(key_is(&map, "name"));
	cbor_value_advance(&map);
	ASSERT(key_is(&map, "qwe"));
	cbor_value_advance(&map);

	/* array */
	ASSERT(key_is(&map, "args"));
	cbor_value_advance(&map);
	ASSERT(cbor_value_is_array(&map));
	ASSERT_EQ(CborNoError, cbor_value_enter_container(&map, &arr));
	ASSERT(key_is(&arr, "a"));
	cbor_value_advance(&arr);
	ASSERT(key_is(&arr, "b"));
	cbor_value_advance(&arr);
	ASSERT(cbor_value_at_end(&arr));
	ASSERT_EQ(CborNoError, cbor_value_leave_container(&map, &arr));

	/* byte string */
	ASSERT(key_is(&map, "blob"));
	cbor_value_advance(&map);
	ASSERT(cbor_value_is_byte_string(&map));
	{
		uint8_t out[8];
		size_t len = sizeof out;
		ASSERT_EQ(CborNoError, cbor_value_copy_byte_string(&map, out, &len, &map));
		ASSERT_EQ(3, (int)len);
		ASSERT_EQ(0, memcmp(out, "\x00\x01\xff", 3));
	}

	/* the tagged value */
	ASSERT(key_is(&map, "pw"));
	cbor_value_advance(&map);
	ASSERT(cbor_value_is_tag(&map));
	{
		CborTag tag;
		ASSERT_EQ(CborNoError, cbor_value_get_tag(&map, &tag));
		ASSERT_EQ(TEST_SECRET_TAG, (unsigned)tag);
	}
	ASSERT_EQ(CborNoError, cbor_value_skip_tag(&map));
	ASSERT(key_is(&map, "hunter2"));
	cbor_value_advance(&map);
	ASSERT(cbor_value_at_end(&map));
	PASS();
}

SUITE(roundtrip)
{
	RUN_TEST(tagged_value_survives);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(roundtrip);
	GREATEST_MAIN_END();
}
