#include "src/edge/yaml/transcode.h"

#include "cbor.h"
#include <yaml.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OK = 0, ERR = -1, NOMEM = -2 };

struct ctx {
	CborEncoder stack[QWE_YAML_MAX_DEPTH + 1];
	int depth; /* index of the innermost open encoder */
	int is_map[QWE_YAML_MAX_DEPTH + 1];
	int expect_key[QWE_YAML_MAX_DEPTH + 1]; /* in a map: next scalar is a key */
	int have_root;
	char *err;
	size_t err_size;
};

static void fail(struct ctx *c, const yaml_mark_t *m, const char *msg)
{
	snprintf(c->err, c->err_size, "%lu:%lu: %s", (unsigned long)m->line + 1,
		 (unsigned long)m->column + 1, msg);
}

static int cbor_rc(CborError e)
{
	return e == CborNoError ? OK : e == CborErrorOutOfMemory ? NOMEM : ERR;
}

static int is_int(const char *s, size_t n)
{
	size_t i = 0;

	if (n > 0 && (s[0] == '-' || s[0] == '+'))
		i = 1;
	if (i == n || n - i > 18)
		return 0;
	for (; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return 0;
	return 1;
}

static int scalar(struct ctx *c, const yaml_event_t *ev, int as_key)
{
	CborEncoder *enc = &c->stack[c->depth];
	const char *v = (const char *)ev->data.scalar.value;
	size_t n = ev->data.scalar.length;
	int plain = ev->data.scalar.style == YAML_PLAIN_SCALAR_STYLE;

	if (!as_key && plain && ev->data.scalar.tag == NULL) {
		if (n == 0 || !strcmp(v, "~") || !strcmp(v, "null") || !strcmp(v, "Null") || !strcmp(v, "NULL"))
			return cbor_rc(cbor_encode_null(enc));
		if (!strcmp(v, "true") || !strcmp(v, "True") || !strcmp(v, "TRUE"))
			return cbor_rc(cbor_encode_boolean(enc, 1));
		if (!strcmp(v, "false") || !strcmp(v, "False") || !strcmp(v, "FALSE"))
			return cbor_rc(cbor_encode_boolean(enc, 0));
		if (is_int(v, n))
			return cbor_rc(cbor_encode_int(enc, strtoll(v, NULL, 10)));
	}
	return cbor_rc(cbor_encode_text_string(enc, v, n));
}

static int open_container(struct ctx *c, const yaml_event_t *ev, int map)
{
	CborError e;

	if (c->depth >= QWE_YAML_MAX_DEPTH) {
		fail(c, &ev->start_mark, "nesting too deep");
		return ERR;
	}
	e = map ? cbor_encoder_create_map(&c->stack[c->depth], &c->stack[c->depth + 1], CborIndefiniteLength)
		: cbor_encoder_create_array(&c->stack[c->depth], &c->stack[c->depth + 1], CborIndefiniteLength);
	c->depth++;
	c->is_map[c->depth] = map;
	c->expect_key[c->depth] = 1;
	return cbor_rc(e);
}

/* After a value lands in a map, the next scalar is a key again. */
static void value_done(struct ctx *c)
{
	if (c->depth > 0 && c->is_map[c->depth])
		c->expect_key[c->depth] = !c->expect_key[c->depth];
}

/* One pass over the whole document. NOMEM means the buffer was too small. */
static int encode(const char *yaml, size_t len, uint8_t *buf, size_t cap, size_t *used, struct ctx *c)
{
	yaml_parser_t p;
	yaml_event_t ev;
	int rc = OK, done = 0, docs = 0;

	memset(c->is_map, 0, sizeof c->is_map);
	c->depth = 0;
	c->have_root = 0;
	cbor_encoder_init(&c->stack[0], buf, cap, 0);
	yaml_parser_initialize(&p);
	yaml_parser_set_input_string(&p, (const unsigned char *)yaml, len);

	while (!done && rc == OK) {
		if (!yaml_parser_parse(&p, &ev)) {
			yaml_mark_t m = p.problem_mark;
			fail(c, &m, p.problem ? p.problem : "invalid YAML");
			rc = ERR;
			break;
		}
		switch (ev.type) {
		case YAML_STREAM_END_EVENT:
			done = 1;
			break;
		case YAML_DOCUMENT_START_EVENT:
			if (++docs > 1) {
				fail(c, &ev.start_mark, "more than one YAML document");
				rc = ERR;
			}
			break;
		case YAML_ALIAS_EVENT:
			fail(c, &ev.start_mark, "aliases are not supported");
			rc = ERR;
			break;
		case YAML_SCALAR_EVENT:
			if (ev.data.scalar.anchor) {
				fail(c, &ev.start_mark, "anchors are not supported");
				rc = ERR;
				break;
			}
			if (c->depth > 0 && c->is_map[c->depth] && c->expect_key[c->depth] &&
			    !strcmp((const char *)ev.data.scalar.value, "<<")) {
				fail(c, &ev.start_mark, "merge keys are not supported");
				rc = ERR;
				break;
			}
			rc = scalar(c, &ev, c->depth > 0 && c->is_map[c->depth] && c->expect_key[c->depth]);
			c->have_root = 1;
			value_done(c);
			break;
		case YAML_MAPPING_START_EVENT:
		case YAML_SEQUENCE_START_EVENT:
			if (ev.type == YAML_MAPPING_START_EVENT ? ev.data.mapping_start.anchor : ev.data.sequence_start.anchor) {
				fail(c, &ev.start_mark, "anchors are not supported");
				rc = ERR;
				break;
			}
			if (c->depth > 0 && c->is_map[c->depth] && c->expect_key[c->depth]) {
				fail(c, &ev.start_mark, "map keys must be scalars");
				rc = ERR;
				break;
			}
			rc = open_container(c, &ev, ev.type == YAML_MAPPING_START_EVENT);
			c->have_root = 1;
			break;
		case YAML_MAPPING_END_EVENT:
		case YAML_SEQUENCE_END_EVENT:
			c->depth--;
			rc = cbor_rc(cbor_encoder_close_container(&c->stack[c->depth], &c->stack[c->depth + 1]));
			value_done(c);
			break;
		default:
			break;
		}
		yaml_event_delete(&ev);
	}
	yaml_parser_delete(&p);
	if (rc == OK && !c->have_root) {
		snprintf(c->err, c->err_size, "1:1: empty document");
		return ERR;
	}
	if (rc == OK)
		*used = cbor_encoder_get_buffer_size(&c->stack[0], buf);
	return rc;
}

int qwe_yaml_to_cbor(const char *yaml, size_t len, uint8_t **out, size_t *out_len,
		     char *err, size_t err_size)
{
	size_t cap = len * 2 + 64, used = 0;
	struct ctx *c = calloc(1, sizeof *c);
	uint8_t *buf = NULL;
	int rc;

	if (!c) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	c->err = err;
	c->err_size = err_size;
	do {
		uint8_t *grown = realloc(buf, cap);
		if (!grown) {
			snprintf(err, err_size, "out of memory");
			free(buf);
			free(c);
			return -1;
		}
		buf = grown;
		rc = encode(yaml, len, buf, cap, &used, c);
		cap *= 2;
	} while (rc == NOMEM);
	free(c);
	if (rc != OK) {
		if (err[0] == '\0')
			snprintf(err, err_size, "cannot encode document");
		free(buf);
		return -1;
	}
	*out = buf;
	*out_len = used;
	return 0;
}
