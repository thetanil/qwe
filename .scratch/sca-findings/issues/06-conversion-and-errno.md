# 06: String-to-number conversion and errno

Status: ready-for-agent
Category: bug
Type: task
Blocked by: 01

## What

**`cert-err34-c` (6 hits).** `src/kernel/workflow.c:1854` does
`ctx.grace_ms = grace_env ? atol(grace_env) : 10000;`, so a malformed
`QWE_TEST_GRACE_MS` silently becomes 0 ms of grace. The other five are in
`src/kernel/proc_test.c` (57, 135, 143, 205, 243), which parses
`/proc/<pid>/stat` and child output. The doc calls that input trusted, but a
test that misparses still passes with a wrong value. Use
`strtol`/`strtoul` with end-pointer and `errno` checks.

**`clang-analyzer-unix.Errno` (3 hits).** `src/kernel/summary.c:122`,
`src/kernel/summary_test.c:24` and `src/edge/yaml/corpus_test.c:24`. The doc
says the `summary.c` hit "traced into an unrelated loop with no `errno` in the
flagged file". It is still there, and it says `errno` "may be overwritten by
function 'malloc'" after an `ftell`/`fseek`. Re-read it with the full
`--raw` path output before accepting that explanation. It sits next to the
`ftell()` bug the first run found.

## Acceptance criteria

- [ ] A malformed `QWE_TEST_GRACE_MS` is rejected or ignored with a diagnostic, never parsed as 0. `unit: <name the test added>` or `e2e: tests/e2e/<case>/`
- [ ] `.clang-tidy` no longer excludes `cert-err34-c` or `clang-analyzer-unix.Errno`, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] The `summary.c:122` verdict (real bug, or a `NOLINTNEXTLINE` with the traced reason) is recorded in the comments. `manual: this ticket's ## Comments`
- [ ] Both rows are gone from the exclusion table, and real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
