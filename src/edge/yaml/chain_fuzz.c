/* libFuzzer target over bytes -> CBOR -> Lua -> validated workflow. */
#include "src/edge/yaml/fuzz_harness.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	qwe_fuzz_chain(data, len);
	return 0;
}
