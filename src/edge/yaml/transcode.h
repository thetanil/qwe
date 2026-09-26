/* YAML -> CBOR, the quarantined edge of qwe.
 *
 * Scalars resolve to null, bool, integer or text by the YAML core rules; map
 * keys are always text. Rejected, with their position: anchors, aliases,
 * merge keys, every tag except !encrypted, more than one document, nesting
 * deeper than QWE_YAML_MAX_DEPTH, input larger than QWE_YAML_MAX_SIZE.
 * `!encrypted "..."` becomes the CBOR tag QWE_SECRET_TAG around the text. */
#ifndef QWE_EDGE_YAML_TRANSCODE_H
#define QWE_EDGE_YAML_TRANSCODE_H

#include "src/edge/yaml/positions.h"
#include "src/edge/yaml/secret_tag.h"

#include <stddef.h>
#include <stdint.h>

#define QWE_YAML_MAX_DEPTH 64
#define QWE_YAML_MAX_SIZE ((size_t)1024 * 1024)

/* On success returns 0 and a malloc'd CBOR buffer the caller frees; if pos is
 * not NULL it also receives the position table (free with
 * qwe_positions_free). On failure returns -1 and fills err ("line:col:
 * message"). A map node's position is that of its first key when it is in
 * block style, and of its "{" in flow style; likewise for sequences. */
int qwe_yaml_to_cbor(const char *yaml, size_t len, uint8_t **out, size_t *out_len,
		     struct qwe_positions **pos, char *err, size_t err_size);

#endif
