# Schema validation runs in Lua, over tables converted from CBOR

The kernel holds workflows and inventories as CBOR in C (TinyCBOR). It validates them with **lua-schema** (pure Lua, MIT, JSON Schema draft-07) running on qwe's own LuaJIT state, not with a C validator. A small C Lua module of our own (`qwe.cbor`) converts CBOR into Lua tables. Validation errors come back as JSON Pointers into the data, and the transcoder's position table, keyed by JSON Pointer and including map-key positions, turns them into `file:line:col` (ADR-0008). Schemas are checked first against a **qwe strict metaschema**: draft-07's metaschema with `additionalProperties: false` at the root, so a typo like `"requried"` is an error rather than an ignored keyword.

We chose this because no usable pure-C99 validator exists. WJElement lacks `const` and has a contradictory license. jsonc-daccord has no metaschema check and doesn't report data locations. The good validators are C++. Writing our own would be about 2–3k lines. The CBOR→Lua conversion has to exist anyway, because plugins receive their `with:` inputs as Lua tables, so validating in Lua adds only a one-time conversion at load. Full comparison: ticket 02's Answer, and `.scratch/m1-engine/research/02-cbor-and-json-schema.md`.

## Consequences

- LuaJIT is required for validation, not only for plugins. `qwe validate` can't run without the embedded Lua runtime.
- The transcoder's position table is keyed by JSON Pointer and records key positions, because `additionalProperties` errors point at the unknown key.
- Plugin input unions are composed as `allOf` of `if: {uses: const X} then: {$ref: X}`, not `oneOf`. lua-schema reports errors from every `oneOf` branch mixed together, so only the matching branch should produce errors.
- `pattern`, `patternProperties` and `format` are removed from the strict metaschema until a real regex engine (PCRE2) is added. They're rejected, never silently ignored.
- lua-schema is beta and has a single maintainer, pinned to an unreleased commit (`1a14a04`). The upstream JSON-Schema-Test-Suite for draft-07 runs as a Bazel test to guard against regressions. QCBOR is the fallback CBOR library, and lua-ConciseSerialization the fallback Lua CBOR library.
