# 05: Workflow schema and `qwe validate`

Status: ready-for-agent
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

- [ ] A valid workflow gives `qwe validate` exit 0 with no output. `e2e: tests/e2e/validate_ok/`
- [ ] An unknown key in a step is reported with `file:line:col` and exit 2. `e2e: tests/e2e/validate_unknown_key/`
- [ ] A `with:` value that violates the plugin's schema is reported at the value's position. `e2e: tests/e2e/validate_with_schema/`
- [ ] A `needs:` cycle is reported, naming the jobs in the cycle. `unit: src/kernel/dag_test.c::cycle_detected`
- [ ] `needs:` naming a job that doesn't exist is an error. `unit: src/kernel/dag_test.c::unknown_need`
- [ ] Duplicate step `id` within a job is an error. `e2e: tests/e2e/validate_duplicate_step_id/`
- [ ] `on:` with any value other than `local` is an error. `e2e: tests/e2e/validate_on_only_local/`
- [ ] A job without `target:` is an error at the job's position. `target: local` is valid with no inventory. `e2e: tests/e2e/validate_target_required/`
- [ ] `uses:` naming an unknown plugin is an error. `e2e: tests/e2e/validate_unknown_plugin/`
- [ ] `qwe run` on an invalid workflow runs nothing (no run directory is created) and exits 2. `e2e: tests/e2e/run_refuses_invalid/`
- [ ] `qwe.cbor` round-trips every CBOR type used, keeps `[]` and `{}` distinct, keeps `null` as the sentinel and the secret tag as the secret type, and rejects integers larger than 2^53 and nesting beyond the limit. `unit: src/kernel/lua_cbor_test.c::roundtrip_types`, `::empty_array_vs_object`, `::rejects_big_int`, `::depth_limit`
- [ ] With two plugins in the union, a bad `with:` key on a `file.ensure` step reports exactly one error, at that key, with no noise from the other plugin's branch. `e2e: tests/e2e/validate_union_single_error/`
- [ ] The upstream JSON-Schema-Test-Suite (draft-07) passes under lua-schema, apart from a listed set of excluded keywords (`pattern`, `patternProperties`, `format`, remote `$ref`). `unit: third_party/lua-schema:json_schema_test_suite`
