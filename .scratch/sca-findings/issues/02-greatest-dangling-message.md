# 02: Dangling greatest message in lifecycle_test

Status: ready-for-agent
Category: bug
Type: task
Blocked by: 01

## What

`docs/static-analysis.md` excludes `clang-analyzer-core.StackAddressEscape`
and `clang-analyzer-core.CallAndMessage` as `greatest` false positives. The
raw run disagrees. All 24 StackAddressEscape hits are in
`src/kernel/lifecycle_test.c` (lines 79 to 452), and they are real.

```c
char where[128];
snprintf(where, sizeof where, "%s x %s", ...);
ASSERTm(where, c->kind != QWE_LC_TRANSITION);
```

`ASSERTm` stores the message pointer in the global `greatest_info` and
returns from the test function. greatest prints the message after that
return, so it reads a dead stack frame, and a failure in these tests
reports a garbage cell name instead of the cell that failed. The fix is a
message buffer that outlives the test function (file-scope `static`), not a
suppression.

The single CallAndMessage hit is `src/kernel/preamble_test.c:131`. Triage it on
its own terms: fix it if it is real, or `NOLINTNEXTLINE` with a comment if it
is a proven false positive.

Then remove both exclusions from `.clang-tidy` and their row from the doc.

## Acceptance criteria

- [ ] A failing `ASSERTm` in `lifecycle_test.c` prints the right cell name. `manual: temporarily invert one ASSERTm in rules_hold_in_every_cell, run bazel test //src/kernel:lifecycle_test, check the failure message names a real "<state> x <event>" cell; revert`
- [ ] `.clang-tidy` no longer excludes `clang-analyzer-core.StackAddressEscape` or `clang-analyzer-core.CallAndMessage`, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] The exclusion-table row is gone, and the bug is recorded under "What the gate found". `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
