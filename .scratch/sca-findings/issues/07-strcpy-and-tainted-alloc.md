# 07: Unbounded strcpy and tainted allocation

Status: ready-for-agent
Category: bug
Type: task
Blocked by: 01

## What

**`clang-analyzer-security.insecureAPI.strcpy` (3 hits):**
`src/kernel/workflow.c:444` (`load_inventory`), `src/kernel/workflow.c:1900`
(the run-directory path) and `src/kernel/redact_test.c:52`. The doc
says each one is bounded by hand-checked arithmetic. With only three sites,
replacing them with `memcpy` on the length already computed, or with a checked
`snprintf` (ticket 05's helper if it exists), costs less than keeping a
name-based exclusion that would let a fourth, unchecked call through.

**`clang-analyzer-optin.taint.TaintedAlloc` (1 hit):** `tools/bcembed.c:88`, an
allocation sized from file contents or argv. It is a build-time tool, but a
size check against a sane maximum (the largest embedded module times some
margin) is one line and makes the finding go away honestly.

## Acceptance criteria

- [ ] No `strcpy`/`strcat` in `src/` or `tools/`. `manual: grep -rnwE 'strcpy|strcat' src tools finds nothing`
- [ ] `bcembed` rejects an input over its size cap with an error. `manual: run bazel-bin/tools/bcembed against an oversized file and confirm it exits non-zero with a message`, or `unit:` if a test harness for `bcembed` exists
- [ ] `.clang-tidy` no longer excludes either check, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] Both rows are gone from the exclusion table. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
