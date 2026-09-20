# 04: YAML→CBOR transcoder: positions, limits, no YAML magic

Status: resolved
Type: task
Blocked by: 03

## What

Harden `src/edge/yaml/` so it's a quarantined edge component:
- Emit a side table **keyed by JSON Pointer** (e.g. `/jobs/build/steps/1/with`) mapping each node to its `line:column`. It records the position of each **map key** as well as each value, because validation errors from lua-schema arrive as JSON Pointers, and unknown-key errors point at the key (ADR-0009).
- Enforce a maximum nesting depth and a maximum document size.
- Reject anchors, aliases and merge keys with their position.
- Preserve the `!encrypted` tag as a CBOR tag, without decrypting anything.

The position table is what every later validation error, and the future `qwe serve` LSP, depend on.

## Acceptance criteria

- [x] Every scalar, map and sequence node has a position, and the positions match the source for a fixture covering flow and block styles. `unit: src/edge/yaml/positions_test.c::block_and_flow_positions`
- [x] Looking up a JSON Pointer returns the value's position, and looking up a key returns the key's own position. Pointer escaping (`~0`, `~1`) is handled. `unit: src/edge/yaml/positions_test.c::pointer_lookup_key_and_value`
- [x] Each of `&anchor`, `*alias` and `<<:` is rejected with an error naming `file:line:col`. `unit: src/edge/yaml/magic_test.c::anchor_rejected`, `::alias_rejected`, `::merge_key_rejected`
- [x] Nesting deeper than the limit is rejected before the document is fully parsed. `unit: src/edge/yaml/limits_test.c::depth_limit`
- [x] A document larger than the size limit is rejected. `unit: src/edge/yaml/limits_test.c::size_limit`
- [x] `!encrypted "v1:…"` becomes the secret CBOR tag with the envelope bytes unchanged. `unit: src/edge/yaml/tags_test.c::encrypted_tag_preserved`
- [x] An unknown tag is rejected with its position. `unit: src/edge/yaml/tags_test.c::unknown_tag_rejected`
- [x] Malformed YAML exits 2 with `file:line:col` in the message. `e2e: tests/e2e/validate_malformed_yaml/`

## Comments

Done. `bazel test //...` passes (19 tests, uncached).
- **API:** `qwe_yaml_to_cbor(..., struct qwe_positions **pos, err, ...)`; `pos` may be NULL. Lookups are `qwe_positions_value` and `qwe_positions_key` by escaped JSON Pointer (`""` is the root). Entries are sorted after the build and found by binary search.
- **Position meaning:** a block map or sequence is positioned at its first key or `-`; a flow one at its `{` or `[`. Scalars at their first character. Lines and columns are 1-based. Tests pin these down.
- **Depth and size:** `QWE_YAML_MAX_DEPTH` 64 (the check runs when the container event arrives, so libyaml never parses the rest) and `QWE_YAML_MAX_SIZE` 1 MiB. The spec named no numbers; both are constants in `transcode.h`.
- **Tags:** `!encrypted` on a scalar value becomes tag `QWE_SECRET_TAG` (32768, `src/edge/yaml/secret_tag.h`) around the unchanged text. Every other tag is rejected with its position, including `!!str`, `!encrypted` on a container, and any tag on a key. Ticket 15 may still move the tag number; there's one definition to change. `qwe.cbor` (Lua bridge) still rejects tagged values until ticket 15.
- **`qwe validate` is parse-only:** it transcodes the file, reports `file:line:col` on failure (exit 2), and otherwise still prints "not implemented" and exits 2, since schema validation is ticket 05. It exists now only so `tests/e2e/validate_malformed_yaml/` has something to run. The golden stderr contains libyaml's own message, so a libyaml upgrade may need it refreshed.
- The transcoder retries the whole parse when its output buffer is too small, rebuilding the position table each time.
