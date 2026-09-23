/* One-input entry points shared by the libFuzzer targets and the corpus replay
 * test. Each must return normally for any bytes at all; a fault, a hang or a
 * leak is the finding, and a violated invariant aborts. */
#ifndef QWE_EDGE_YAML_FUZZ_HARNESS_H
#define QWE_EDGE_YAML_FUZZ_HARNESS_H

#include <stddef.h>
#include <stdint.h>

/* bytes -> qwe_yaml_to_cbor. Checks that the CBOR is well formed. */
void qwe_fuzz_transcode(const uint8_t *data, size_t len);

/* bytes -> qwe_yaml_to_cbor -> qwe_cbor_to_lua -> qwe_validate_doc, against a
 * workflow directory that does not exist so no plugin is read from disk. Keeps one
 * Lua state for the life of the process, and sends validation's stderr to
 * /dev/null. */
void qwe_fuzz_chain(const uint8_t *data, size_t len);

/* Closes qwe_fuzz_chain's Lua state early. The fuzz binaries never call this
 * (see fuzz_harness.c); corpus_test.c calls it once, after its replay is
 * done, so nothing it allocated outlives the process as an apparent leak. */
void qwe_fuzz_chain_close(void);

#endif
