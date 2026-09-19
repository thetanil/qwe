#include "src/edge/yaml/transcode.h"

#include "cbor.h"
#include <yaml.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OK = 0, ERR = -1, NOMEM = -2 };

#define ENCRYPTED_TAG "!encrypted"

struct ctx {
	CborEncoder stack[QWE_YAML_MAX_DEPTH + 1];
	int depth; /* index of the innermost open encoder; 0 is the document */
	int is_map[QWE_YAML_MAX_DEPTH + 1];
	int expect_key[QWE_YAML_MAX_DEPTH + 1]; /* in a map: next scalar is a key */
	int have_root;
	char *err;
	size_t err_size;

	/* Position table and the JSON Pointer of the node being visited. */
	struct qwe_positions *pos;
	char *path;
	size_t path_len, path_cap;
	size_t container_len[QWE_YAML_MAX_DEPTH + 2]; /* pointer length of each container */
	unsigned long next_index[QWE_YAML_MAX_DEPTH + 2];
	long key_entry[QWE_YAML_MAX_DEPTH + 2]; /* table entry of the key just read */
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

static struct qwe_pos to_pos(const yaml_mark_t *m)
{
	struct qwe_pos p;

	p.line = (unsigned)m->line + 1;
	p.col = (unsigned)m->column + 1;
	return p;
}

/* --- JSON Pointer bookkeeping --- */

static int path_append(struct ctx *c, const char *s, size_t n)
{
	if (c->path_len + n + 1 > c->path_cap) {
		size_t cap = c->path_cap ? c->path_cap * 2 : 128;
		char *grown;
		while (cap < c->path_len + n + 1)
			cap *= 2;
		grown = realloc(c->path, cap);
		if (!grown)
			return NOMEM;
		c->path = grown;
		c->path_cap = cap;
	}
	memcpy(c->path + c->path_len, s, n);
	c->path_len += n;
	return OK;
}

/* Appends "/" and one reference token, escaping "~" and "/". */
static int path_push_key(struct ctx *c, const char *key, size_t n)
{
	size_t i;
	int rc = path_append(c, "/", 1);

	for (i = 0; rc == OK && i < n; i++) {
		if (key[i] == '~')
			rc = path_append(c, "~0", 2);
		else if (key[i] == '/')
			rc = path_append(c, "~1", 2);
		else
			rc = path_append(c, key + i, 1);
	}
	return rc;
}

static int path_push_index(struct ctx *c, unsigned long i)
{
	char buf[32];
	int n = snprintf(buf, sizeof buf, "/%lu", i);

	return path_append(c, buf, (size_t)n);
}

/* The parent container's pointer, again. */
static void path_reset(struct ctx *c)
{
	c->path_len = c->depth > 0 ? c->container_len[c->depth] : 0;
}

/* Records where the value that is starting sits, and leaves the pointer of
 * that value in c->path. */
static int note_value(struct ctx *c, const yaml_mark_t *m)
{
	struct qwe_pos p = to_pos(m);
	int rc = OK;

	if (c->depth == 0) {
		c->path_len = 0;
		if (qwe_positions_add(c->pos, "", 0, NULL, &p) < 0)
			rc = NOMEM;
	} else if (c->is_map[c->depth]) {
		/* The key already added the entry and set the pointer. */
		qwe_positions_set_value(c->pos, c->key_entry[c->depth], p);
	} else {
		path_reset(c);
		rc = path_push_index(c, c->next_index[c->depth]++);
		if (rc == OK && qwe_positions_add(c->pos, c->path, c->path_len, NULL, &p) < 0)
			rc = NOMEM;
	}
	return rc;
}

static int note_key(struct ctx *c, const yaml_event_t *ev)
{
	struct qwe_pos p = to_pos(&ev->start_mark);
	int rc;

	path_reset(c);
	rc = path_push_key(c, (const char *)ev->data.scalar.value, ev->data.scalar.length);
	if (rc != OK)
		return rc;
	c->key_entry[c->depth] = qwe_positions_add(c->pos, c->path, c->path_len, &p, NULL);
	return c->key_entry[c->depth] < 0 ? NOMEM : OK;
}

/* --- scalars --- */

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
	const char *tag = (const char *)ev->data.scalar.tag;
	int plain = ev->data.scalar.style == YAML_PLAIN_SCALAR_STYLE;

	if (tag) {
		int rc = cbor_rc(cbor_encode_tag(enc, QWE_SECRET_TAG));
		return rc != OK ? rc : cbor_rc(cbor_encode_text_string(enc, v, n));
	}
	if (!as_key && plain) {
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
	c->container_len[c->depth] = c->path_len; /* the pointer note_value left */
	c->next_index[c->depth] = 0;
	return cbor_rc(e);
}

/* After a value lands in a map, the next scalar is a key again. */
static void value_done(struct ctx *c)
{
	if (c->depth > 0 && c->is_map[c->depth])
		c->expect_key[c->depth] = !c->expect_key[c->depth];
}

/* Checks the anchor and tag of a node that may not carry them. */
static int check_props(struct ctx *c, const yaml_mark_t *m, const yaml_char_t *anchor,
		       const yaml_char_t *tag, int tag_ok)
{
	if (anchor) {
		fail(c, m, "anchors are not supported");
		return ERR;
	}
	if (tag && !(tag_ok && !strcmp((const char *)tag, ENCRYPTED_TAG))) {
		char msg[128];
		snprintf(msg, sizeof msg, "unknown tag %.80s", (const char *)tag);
		fail(c, m, msg);
		return ERR;
	}
	return OK;
}

static int handle_event(struct ctx *c, yaml_event_t *ev, int *docs)
{
	int in_map = c->depth > 0 && c->is_map[c->depth];
	int is_key = in_map && c->expect_key[c->depth];
	int rc;

	switch (ev->type) {
	case YAML_DOCUMENT_START_EVENT:
		if (++*docs > 1) {
			fail(c, &ev->start_mark, "more than one YAML document");
			return ERR;
		}
		return OK;
	case YAML_ALIAS_EVENT:
		fail(c, &ev->start_mark, "aliases are not supported");
		return ERR;
	case YAML_SCALAR_EVENT:
		rc = check_props(c, &ev->start_mark, ev->data.scalar.anchor, ev->data.scalar.tag, !is_key);
		if (rc != OK)
			return rc;
		if (is_key && ev->data.scalar.style == YAML_PLAIN_SCALAR_STYLE &&
		    !strcmp((const char *)ev->data.scalar.value, "<<")) {
			fail(c, &ev->start_mark, "merge keys are not supported");
			return ERR;
		}
		rc = is_key ? note_key(c, ev) : note_value(c, &ev->start_mark);
		if (rc == OK)
			rc = scalar(c, ev, is_key);
		c->have_root = 1;
		value_done(c);
		return rc;
	case YAML_MAPPING_START_EVENT:
	case YAML_SEQUENCE_START_EVENT: {
		int map = ev->type == YAML_MAPPING_START_EVENT;
		rc = check_props(c, &ev->start_mark, map ? ev->data.mapping_start.anchor : ev->data.sequence_start.anchor,
				 map ? ev->data.mapping_start.tag : ev->data.sequence_start.tag, 0);
		if (rc != OK)
			return rc;
		if (is_key) {
			fail(c, &ev->start_mark, "map keys must be scalars");
			return ERR;
		}
		/* Depth first, so a too-deep document fails before it is parsed further. */
		if (c->depth >= QWE_YAML_MAX_DEPTH) {
			fail(c, &ev->start_mark, "nesting too deep");
			return ERR;
		}
		rc = note_value(c, &ev->start_mark);
		if (rc == OK)
			rc = open_container(c, ev, map);
		c->have_root = 1;
		return rc;
	}
	case YAML_MAPPING_END_EVENT:
	case YAML_SEQUENCE_END_EVENT:
		c->depth--;
		rc = cbor_rc(cbor_encoder_close_container(&c->stack[c->depth], &c->stack[c->depth + 1]));
		value_done(c);
		return rc;
	default:
		return OK;
	}
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
	c->path_len = 0;
	qwe_positions_free(c->pos);
	c->pos = qwe_positions_new();
	if (!c->pos)
		return ERR;
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
		if (ev.type == YAML_STREAM_END_EVENT)
			done = 1;
		else
			rc = handle_event(c, &ev, &docs);
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
		     struct qwe_positions **pos, char *err, size_t err_size)
{
	size_t cap = len * 2 + 64, used = 0;
	struct ctx *c;
	uint8_t *buf = NULL;
	int rc;

	if (len > QWE_YAML_MAX_SIZE) {
		snprintf(err, err_size, "1:1: document is larger than %d bytes", QWE_YAML_MAX_SIZE);
		return -1;
	}
	c = calloc(1, sizeof *c);
	if (!c) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	c->err = err;
	c->err_size = err_size;
	err[0] = '\0';
	do {
		uint8_t *grown = realloc(buf, cap);
		if (!grown) {
			snprintf(err, err_size, "out of memory");
			free(buf);
			qwe_positions_free(c->pos);
			free(c->path);
			free(c);
			return -1;
		}
		buf = grown;
		rc = encode(yaml, len, buf, cap, &used, c);
		cap *= 2;
	} while (rc == NOMEM);
	free(c->path);
	if (rc != OK) {
		if (err[0] == '\0')
			snprintf(err, err_size, "cannot encode document");
		free(buf);
		qwe_positions_free(c->pos);
		free(c);
		return -1;
	}
	qwe_positions_finish(c->pos);
	if (pos)
		*pos = c->pos;
	else
		qwe_positions_free(c->pos);
	free(c);
	*out = buf;
	*out_len = used;
	return 0;
}
