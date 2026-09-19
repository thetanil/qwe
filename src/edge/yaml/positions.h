/* Side table from JSON Pointer to source position, built by the transcoder.
 * It holds the position of every value and, for map entries, of the key too
 * (unknown-key errors point at the key). Lines and columns are 1-based.
 * Pointers use RFC 6901 escaping: "~" is "~0" and "/" is "~1". */
#ifndef QWE_EDGE_YAML_POSITIONS_H
#define QWE_EDGE_YAML_POSITIONS_H

#include <stddef.h>

struct qwe_pos {
	unsigned line, col;
};

struct qwe_positions;

struct qwe_positions *qwe_positions_new(void);
void qwe_positions_free(struct qwe_positions *p);
size_t qwe_positions_count(const struct qwe_positions *p);

/* Lookups return 0 and fill *out, or -1 if the pointer or that position kind
 * is not in the table. The root document's pointer is "". */
int qwe_positions_value(const struct qwe_positions *p, const char *pointer, struct qwe_pos *out);
int qwe_positions_key(const struct qwe_positions *p, const char *pointer, struct qwe_pos *out);

/* Builder interface, for the transcoder. */
long qwe_positions_add(struct qwe_positions *p, const char *pointer, size_t len,
		       const struct qwe_pos *key, const struct qwe_pos *value);
void qwe_positions_set_value(struct qwe_positions *p, long index, struct qwe_pos value);
/* Sorts the table for lookup. Indexes from qwe_positions_add die here. */
void qwe_positions_finish(struct qwe_positions *p);

#endif
