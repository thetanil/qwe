# 21: Recursion

Status: ready-for-agent
Category: enhancement
Type: task

## What

`misc-no-recursion` (not enabled today) reports 5 functions in a recursive call
chain in `src/` (2026-09-26, `bb9d69f`):

- `dag.c:30` `visit` (cycle detection in the job graph),
- `luacbor.c:73` `convert_container`, `:127` `convert`, `:228` `encode_table`,
  `:327` `encode_value` (the CBOR <-> Lua converters, which recurse into nested
  values).

Round 2 would have written "depth-limited, so bounded". That is a justification,
and it is true: `QWE_LUA_MAX_DEPTH` (`luacbor.h:18`) is 64 for the CBOR <-> Lua
converters (the YAML side has its own `QWE_YAML_MAX_DEPTH`, already an explicit
stack in `transcode.c`). Recursion is still prohibited by most
safety coding standards (MISRA C:2012 rule 17.2, ISO 26262-6 Table 6; the worst-case stack usage
of recursion cannot be computed by a tool, only argued). And an explicit stack is
simpler to bound and to test.

## Fix

Rewrite each with an explicit work stack. Where a constant bounds the depth
today (`luacbor.c`), the stack is a fixed-size local array of that size (the
depth limit becomes an array size, not a runtime counter; no heap, so nothing to
leak if a Lua call raises mid-walk, ticket 02), with the same error message on
overflow so the e2e goldens do not change.

- `dag.c` `visit`: an iterative DFS with a colour array and a stack of
  `(node, next_edge)`. There is no compile-time bound on the number of jobs, so
  the stack is `njobs` long and heap-allocated (not a VLA: ticket 08 turns on
  `-Wvla`). That adds an allocation-failure path: it gets an OOM case (the
  existing `oom_shim` sweep over the validate/run paths reaches it; check the
  sweep count grows by one and the failure is reported, not crashed on).
- `luacbor.c`: decoding into Lua and encoding from Lua both walk a tree with
  values that need a container-close step after their children. Use an
  explicit stack of `{CborValue iterator, kind, table index}` frames.
  **Encoding `encode_table` is also among the worst complexities in ticket 22
  (56): ticket 22 is blocked by this one, so split `encode_table` to a
  complexity under 25 in this rewrite; 22 then only confirms it.**

Add `misc-no-recursion` to `.clang-tidy` `Checks` (no exclusion, no option).

## Acceptance criteria

- [ ] `misc-no-recursion` is on and the gate exits 0. `manual: run.sh`
- [ ] A document nested exactly at the limit still converts, one deeper still
      fails with the same message, in both directions (CBOR to Lua, Lua to CBOR)
      and for a job graph with a long chain (e.g. 1000 jobs, one `needs` each)
      and a cycle closed by its last edge.
      `unit: src/kernel/lua_cbor_test.c`, `src/kernel/dag_test.c` (red first where
      the existing test does not already check the boundary)
- [ ] The e2e goldens are unchanged. `manual: bazel test //tests/e2e/...`
- [ ] The fuzz corpus still passes: `bazel test //src/edge/yaml/...` and the
      corpus test. `unit: the corpus tests`
- [ ] `bazel test //...` (also `--config=asan`) and the coverage check are green.

## Comments
