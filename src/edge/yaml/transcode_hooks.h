/* For tests only: drives the transcoder's event handler directly, as libyaml
 * would, so that events libyaml itself never emits can be tried. */
#ifndef QWE_EDGE_YAML_TRANSCODE_HOOKS_H
#define QWE_EDGE_YAML_TRANSCODE_HOOKS_H

#include <stddef.h>
#include <stdint.h>
#include <yaml.h>

/* Feeds events[0..n) in order. Returns 0 with the CBOR they encode in *out (a
 * static buffer, not to be freed) and its length, or -1 with err filled. */
int qwe_yaml_events_for_test(yaml_event_t *events, size_t n, uint8_t **out, size_t *out_len, char *err,
			     size_t err_size);

#endif
