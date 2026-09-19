/* YAML -> CBOR. Anchors, aliases and merge keys are rejected. Scalars resolve
 * to null, bool, integer or text by the YAML core rules; map keys are always
 * text. Source positions and limits are ticket 04's. */
#ifndef QWE_EDGE_YAML_TRANSCODE_H
#define QWE_EDGE_YAML_TRANSCODE_H

#include <stddef.h>
#include <stdint.h>

#define QWE_YAML_MAX_DEPTH 64

/* On success returns 0 and a malloc'd buffer the caller frees. On failure
 * returns -1 and fills err ("line:col: message"). */
int qwe_yaml_to_cbor(const char *yaml, size_t len, uint8_t **out, size_t *out_len,
		     char *err, size_t err_size);

#endif
