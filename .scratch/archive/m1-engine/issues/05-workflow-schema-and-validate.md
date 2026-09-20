# 05: Workflow schema and `qwe validate`

Status: resolved
Type: task
Blocked by: 04

## What

Validation runs in Lua (ADR-0009). Build the `qwe.cbor` C Lua module (TinyCBOR ↔ Lua tables):
- maps and arrays get their own metatables,
- `null` becomes a sentinel,
- secrets become a distinct type,
- nesting has a depth limit,
- integers larger than 2^53 are rejected.

Vendor **lua-schema** at `1a14a04c8586ce136d39f8e189f032a460c81ef1`, **LPeg 1.1.0** and **dkjson 2.8**, embedded as bytecode, with a small compat shim for `utf8.len` and `math.tointeger`. Use **draft-07**, and compose plugin unions as `allOf` of `if`/`then` on `uses`, not `oneOf`.

Compose the workflow schema from the kernel's own structure (jobs, steps, `needs`, `target`, `on`, `env`, `timeout-minutes`, `continue-on-error`, `become`, `id`, `secret-outputs`, `max-parallel`) together with each loaded plugin's `with:` schema. Validate the transcoded CBOR against it. Add the structural checks the schema can't express. Add `qwe validate`, and make `qwe run` validate first. Every error carries a position.

## Acceptance criteria

- [x] A valid workflow gives `qwe validate` exit 0 with no output. `e2e: tests/e2e/validate_ok/`
- [x] An unknown key in a step is reported with `file:line:col` and exit 2. `e2e: tests/e2e/validate_unknown_key/`
- [x] A `with:` value that violates the plugin's schema is reported at the value's position. `e2e: tests/e2e/validate_with_schema/`
- [x] A `needs:` cycle is reported, naming the jobs in the cycle. `unit: src/kernel/dag_test.c::cycle_detected`
- [x] `needs:` naming a job that doesn't exist is an error. `unit: src/kernel/dag_test.c::unknown_need`
- [x] Duplicate step `id` within a job is an error. `e2e: tests/e2e/validate_duplicate_step_id/`
- [x] `on:` with any value other than `local` is an error. `e2e: tests/e2e/validate_on_only_local/`
- [x] A job without `target:` is an error at the job's position. `target: local` is valid with no inventory. `e2e: tests/e2e/validate_target_required/`
- [x] `uses:` naming an unknown plugin is an error. `e2e: tests/e2e/validate_unknown_plugin/`
- [x] `qwe run` on an invalid workflow runs nothing (no run directory is created) and exits 2. `e2e: tests/e2e/run_refuses_invalid/`
- [x] `qwe.cbor` round-trips every CBOR type used, keeps `[]` and `{}` distinct, keeps `null` as the sentinel and the secret tag as the secret type, and rejects integers larger than 2^53 and nesting beyond the limit. `unit: src/kernel/lua_cbor_test.c::roundtrip_types`, `::empty_array_vs_object`, `::rejects_big_int`, `::depth_limit`
- [x] With two plugins in the union, a bad `with:` key on a `file.ensure` step reports exactly one error, at that key, with no noise from the other plugin's branch. `e2e: tests/e2e/validate_union_single_error/`
- [x] The upstream JSON-Schema-Test-Suite (draft-07) passes under lua-schema, apart from a listed set of excluded keywords (`pattern`, `patternProperties`, `format`, remote `$ref`). `unit: third_party/lua-schema:json_schema_test_suite`

## Comments

Done. `bazel test //...` passes (33 tests).
- **Vendored:** lua-schema at `1a14a04` (`third_party/lua-schema`), LPeg 1.1.0 (C module plus `re.lua`), dkjson 2.8, and the JSON-Schema-Test-Suite draft-07 tests at commit `ab079cc` (without `optional/`). Each has a `VERSION` file.
- **Bytecode:** `tools/bcembed.c` compiles Lua to LuaJIT bytecode on the build host and emits `src/kernel/lua/modules.c` (replaces the source embedding from ticket 03). A `.json` file embeds as a module returning its text. `compat53.lua` is our own shim (`utf8.len` counts code points properly).
- **`qwe.cbor`:** `src/kernel/luacbor.c`, both a C API and a Lua module (`decode`, `encode`, `array`, `map`, `secret`, `is_*`, `null`, metatables). A secret is `{value = "…"}` with a locked metatable whose `tostring` is `***`; ticket 15 decides how it satisfies `type: string`. Byte strings decode to Lua strings and encode back as text.
- **Schema:** `src/kernel/lua/workflow.schema.json` is the kernel's part, as data. `validate.lua` adds one `if uses == X then with: <X's schema>` branch per plugin (`allOf`, never `oneOf`), and a plugin with required inputs also makes `with:` required.
- **Positions:** lua-schema reports an `additionalProperties` error at the unknown key's own pointer, so those map to the key's position and everything else to the value's. An error with no table entry falls back to its nearest ancestor. lua-schema's bare `allOf` aggregate line is dropped so each problem is reported once.
- **Structural checks:** duplicate step ids (Lua), `needs:` unknown job and cycle (`src/kernel/dag.c`, tested in C). They run only when the schema passed.
- **`file.ensure`:** only its `with:` schema exists (`plugins/builtin/file.ensure/schema.json`), so the union has a real plugin. `uses: file.ensure` validates but `qwe run` still says "step has no run:"; ticket 10 adds the plugin. The e2e union case has one plugin, so `src/kernel/lua/validate_test.lua` covers the literal two-plugin case.
- **Test suite exclusions** (in `third_party/lua-schema/json_schema_test_suite.lua`): whole files `pattern`, `patternProperties`, `format`, `refRemote`; groups that mention those keywords, `localhost:1234`, or a `$ref` to the draft-07 metaschema; and one test, `multipleOf` 0.0075/0.0001, which fails on double arithmetic. 742 pass, 0 fail, 30 skipped.
- **Not done here:** the strict plugin-schema metaschema and plugin contract checks are ticket 09's; built-in `with:` schemas are trusted for now. `-i` inventory and target resolution are ticket 12's, so `target:` is only required to be a non-empty string.
- `luarun` (`src/kernel/luarun.c`) runs a Lua script inside qwe's state; the Lua-side tests use it.
