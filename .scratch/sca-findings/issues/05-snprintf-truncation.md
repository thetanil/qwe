# 05: snprintf/sprintf truncation

Status: ready-for-agent
Category: bug
Type: task
Blocked by: 04

## What

Add `::snprintf;::sprintf` to ticket 04's `cert-err33-c` `CheckedFunctions`. There
are 96 hits.

- **`sprintf`, 5 hits:** `src/kernel/validate.c` (3) and `src/kernel/workflow.c` (2).
  These have no bound at all. Convert each one to a bounded call.
- **`snprintf` in production code:** `secrets/keyfile.c` (11),
  `edge/yaml/transcode.c` (8), `kernel/workflow.c` (5), `luacbor.c` (4),
  `dag.c` (3), `trace.c` (2), `sink.c`, `validate.c`, `tools/bcembed.c`. A
  truncated path or key name is a wrong answer, not a cosmetic one, so each
  of these either checks `ret < 0 || ret >= size` and fails, or has a comment
  proving it cannot truncate.
- **`snprintf` in tests, about 50 hits:** mostly building fixture paths. A small
  helper that asserts no truncation is likely cheaper than 50 inline checks.

If the same check-and-fail pattern repeats across `src/`, a single
`qwe_snprintf_checked` (or similar) helper in `src/kernel/` is the right
shape. Then the check sees one checked call instead of dozens.

## Acceptance criteria

- [ ] No `sprintf` in `src/` or `tools/`. `manual: grep -rnw sprintf src tools finds only snprintf/vsnprintf`
- [ ] `CheckedFunctions` includes `snprintf` and `sprintf`, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] At least one production truncation path is exercised: an over-long input is rejected with an error. `unit: <name the test added>`
- [ ] Real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
