# 04: YAML→CBOR transcoder: positions, limits, no YAML magic

Status: ready-for-agent
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

- [ ] Every scalar, map and sequence node has a position, and the positions match the source for a fixture covering flow and block styles. `unit: src/edge/yaml/positions_test.c::block_and_flow_positions`
- [ ] Looking up a JSON Pointer returns the value's position, and looking up a key returns the key's own position. Pointer escaping (`~0`, `~1`) is handled. `unit: src/edge/yaml/positions_test.c::pointer_lookup_key_and_value`
- [ ] Each of `&anchor`, `*alias` and `<<:` is rejected with an error naming `file:line:col`. `unit: src/edge/yaml/magic_test.c::anchor_rejected`, `::alias_rejected`, `::merge_key_rejected`
- [ ] Nesting deeper than the limit is rejected before the document is fully parsed. `unit: src/edge/yaml/limits_test.c::depth_limit`
- [ ] A document larger than the size limit is rejected. `unit: src/edge/yaml/limits_test.c::size_limit`
- [ ] `!encrypted "v1:…"` becomes the secret CBOR tag with the envelope bytes unchanged. `unit: src/edge/yaml/tags_test.c::encrypted_tag_preserved`
- [ ] An unknown tag is rejected with its position. `unit: src/edge/yaml/tags_test.c::unknown_tag_rejected`
- [ ] Malformed YAML exits 2 with `file:line:col` in the message. `e2e: tests/e2e/validate_malformed_yaml/`
