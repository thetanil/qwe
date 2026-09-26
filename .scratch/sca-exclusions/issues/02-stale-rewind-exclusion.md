# 02: Remove the stale `rewind` exclusion

Status: ready-for-agent
Category: enhancement
Type: task

## What

`bugprone-unsafe-functions`, `cert-msc24-c` and `cert-msc33-c` are excluded
because they were "only ever `rewind()`". `sca-findings/06` replaced every
`rewind` in `src/`, `tools/` and the tests with a checked
`fseek(fp, 0, SEEK_SET)`. The raw run at `2d56de5` has 0 findings for all
three checks. The exclusion no longer excludes anything, but it would still
let the next `rewind`, `gets`, `tmpnam` or `setbuf` through without a finding.

Remove the three lines from `.clang-tidy` and the row from
`docs/static-analysis.md`. `bugprone-unsafe-functions` runs with
`ReportMoreUnsafeFunctions: true` by default, so check the gate on the whole
tree, not just the three files `rewind` used to be in.

## Acceptance criteria

- [ ] `.clang-tidy` no longer excludes `bugprone-unsafe-functions`, `cert-msc24-c` or `cert-msc33-c`, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] A `rewind` call fails the gate. `manual: add a throwaway rewind(fp) to src/kernel/summary.c's log_tail, run the gate, see it exit non-zero naming bugprone-unsafe-functions; revert`
- [ ] The exclusion-table row is gone. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
