#include "cbor.h"
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/edge/yaml/transcode.h"
#include "src/edge/yaml/transcode_hooks.h"

#include <stdlib.h>
#include <string.h>

static int to_cbor(const char *y, uint8_t **buf, size_t *n, char *err)
{
	return qwe_yaml_to_cbor(y, strlen(y), buf, n, NULL, err, 128);
}

/* Compares the transcoded bytes with an indefinite-length CBOR literal. */
TEST maps_seqs_and_scalars(void)
{
	static const uint8_t want[] = {
		0xbf,				 /* map(*) */
		0x61, 'a', 0x9f, 0x01, 0xf5, 0xf6, 0x61, 'x', 0xff, /* "a": [1, true, null, "x"] */
		0x61, 'k', 0x63, '1', '2', '3', /* "k": "123" (quoted below) */
		0xff};
	uint8_t *buf;
	size_t n;
	char err[128] = "";

	ASSERT_EQ(0, to_cbor("a: [1, true, ~, x]\nk: \"123\"\n", &buf, &n, err));
	ASSERT_EQ(sizeof want, n);
	ASSERT_EQ(0, memcmp(want, buf, n));
	free(buf);
	PASS();
}

TEST rejects_anchors_aliases_merge(void)
{
	uint8_t *buf;
	size_t n;
	char err[128] = "";

	ASSERT_EQ(-1, to_cbor("a: &x 1\n", &buf, &n, err));
	ASSERT_EQ(-1, to_cbor("a: &x 1\nb: *x\n", &buf, &n, err));
	ASSERT_EQ(-1, to_cbor("a: {<<: {b: 1}}\n", &buf, &n, err));
	PASS();
}

TEST reports_position(void)
{
	uint8_t *buf;
	size_t n;
	char err[128] = "";

	ASSERT_EQ(-1, to_cbor("a: [1, 2\nb: 3\n", &buf, &n, err));
	ASSERT(strchr(err, ':') != NULL);
	ASSERT(err[0] >= '1' && err[0] <= '9');
	PASS();
}

TEST floats_resolve(void)
{
	uint8_t *buf;
	size_t n;
	char err[128] = "";
	CborParser parser;
	CborValue it, arr;
	double d;
	size_t i;
	static const char *const yes[] = {"0.01", "-1.5", "+.5", "2.", "1e3", "1.5E-2"};
	static const double want[] = {0.01, -1.5, 0.5, 2.0, 1000.0, 0.015};
	static const char *const no[] = {"1.2.3", "e5", "1e", ".", "-", "1_000", "0x10", "1.5x"};

	/* Each of these is a float. */
	for (i = 0; i < sizeof yes / sizeof yes[0]; i++) {
		char doc[32];
		qwe_xfmt(doc, sizeof doc, "[%s]", yes[i]);
		ASSERT_EQ(0, to_cbor(doc, &buf, &n, err));
		ASSERT_EQ(CborNoError, cbor_parser_init(buf, n, 0, &parser, &it));
		ASSERT_EQ(CborNoError, cbor_value_enter_container(&it, &arr));
		ASSERT(cbor_value_is_double(&arr) || cbor_value_is_float(&arr));
		ASSERT_EQ(CborNoError, cbor_value_get_double(&arr, &d));
		ASSERT_IN_RANGE(want[i], d, 1e-12);
		free(buf);
	}
	/* And these are plain strings. */
	for (i = 0; i < sizeof no / sizeof no[0]; i++) {
		char doc[32];
		qwe_xfmt(doc, sizeof doc, "[%s]", no[i]);
		ASSERT_EQ(0, to_cbor(doc, &buf, &n, err));
		ASSERT_EQ(CborNoError, cbor_parser_init(buf, n, 0, &parser, &it));
		ASSERT_EQ(CborNoError, cbor_value_enter_container(&it, &arr));
		ASSERT(cbor_value_is_text_string(&arr));
		free(buf);
	}
	PASS();
}

TEST unbalanced_end_is_refused(void)
{
	yaml_event_t ev[2];
	uint8_t *out;
	size_t out_len;
	char err[128] = "";

	/* an end with nothing open: libyaml never emits it, and it must not index below the stack */
	yaml_document_start_event_initialize(&ev[0], NULL, NULL, NULL, 1);
	yaml_mapping_end_event_initialize(&ev[1]);
	ASSERT_EQ(-1, qwe_yaml_events_for_test(ev, 2, &out, &out_len, err, sizeof err));
	ASSERT(strstr(err, "unbalanced") != NULL);
	ASSERT(strncmp(err, "1:1:", 4) == 0); /* positioned like every other error */
	yaml_event_delete(&ev[0]);

	yaml_document_start_event_initialize(&ev[0], NULL, NULL, NULL, 1);
	yaml_sequence_end_event_initialize(&ev[1]);
	ASSERT_EQ(-1, qwe_yaml_events_for_test(ev, 2, &out, &out_len, err, sizeof err));
	yaml_event_delete(&ev[0]);

	/* and an end of the wrong kind for what is open */
	{
		yaml_event_t seq[3];

		yaml_document_start_event_initialize(&seq[0], NULL, NULL, NULL, 1);
		yaml_sequence_start_event_initialize(&seq[1], NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE);
		yaml_mapping_end_event_initialize(&seq[2]);
		ASSERT_EQ(-1, qwe_yaml_events_for_test(seq, 3, &out, &out_len, err, sizeof err));
		yaml_event_delete(&seq[0]);
		yaml_event_delete(&seq[1]);
	}
	PASS();
}

TEST embedded_nul_is_text(void)
{
	static const char value[] = "null\0x"; /* 6 bytes: a prefix match would call it null */
	yaml_event_t ev[4];
	uint8_t *out;
	size_t out_len, len = 0;
	char err[128] = "";
	CborParser parser;
	CborValue it, elem;

	yaml_document_start_event_initialize(&ev[0], NULL, NULL, NULL, 1);
	yaml_sequence_start_event_initialize(&ev[1], NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE);
	yaml_scalar_event_initialize(&ev[2], NULL, NULL, (yaml_char_t *)value, 6, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	yaml_sequence_end_event_initialize(&ev[3]);
	ASSERT_EQ(0, qwe_yaml_events_for_test(ev, 4, &out, &out_len, err, sizeof err));
	ASSERT_EQ(CborNoError, cbor_parser_init(out, out_len, 0, &parser, &it));
	ASSERT(cbor_value_is_array(&it));
	ASSERT_EQ(CborNoError, cbor_value_enter_container(&it, &elem));
	ASSERT(cbor_value_is_text_string(&elem));
	ASSERT_EQ(CborNoError, cbor_value_calculate_string_length(&elem, &len));
	ASSERT_EQ(6, (int)len);
	yaml_event_delete(&ev[0]);
	yaml_event_delete(&ev[1]);
	yaml_event_delete(&ev[2]);
	PASS();
}

SUITE(transcode)
{
	RUN_TEST(maps_seqs_and_scalars);
	RUN_TEST(unbalanced_end_is_refused);
	RUN_TEST(embedded_nul_is_text);
	RUN_TEST(rejects_anchors_aliases_merge);
	RUN_TEST(reports_position);
	RUN_TEST(floats_resolve);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(transcode);
	GREATEST_MAIN_END();
}
