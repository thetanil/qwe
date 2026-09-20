/* libFuzzer target over the YAML transcoder alone. */
#include "src/edge/yaml/fuzz_harness.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	qwe_fuzz_transcode(data, len);
	return 0;
}
