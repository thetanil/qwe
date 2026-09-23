# 01: A clang-tidy/Clang-Static-Analyzer gate, wired in alongside valgrind

Status: resolved
Category: enhancement
Type: task

## What

Stand up `tools/clang-tidy/run.sh` and `.clang-tidy` (see `spec.md` for the
approach) and wire a `static-analysis` job into `.github/workflows/valgrind.yml`
so it runs on valgrind's own cadence (by hand, nightly, in a release).

## Acceptance criteria

- [x] `tools/clang-tidy/run.sh` runs clean (exit 0) against the current tree. `manual: cd /workspace/thetanil/qwe && CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] The gate is wired into CI alongside valgrind, same cadence, no separate badge. `manual: .github/workflows/valgrind.yml job "static-analysis"`; `unit: bazel test //tools/ci:workflows_test`
- [x] Every exclusion from the raw checks defaults is justified against this codebase, not just silenced. `manual: docs/static-analysis.md`'s exclusion table
- [x] `bazel test //...` is still green after the fixes the gate's first run drove. `unit: bazel test //...`
- [x] Real findings from the first run are fixed, not suppressed to get a green gate. `manual: docs/static-analysis.md`, "What the gate found on first run"

## Comments

### Resolved

Implementation is `tools/clang-tidy/run.sh` + `.clang-tidy`; the reasoning and
the full exclusion list are `docs/static-analysis.md`; the CI wiring is
`.github/workflows/valgrind.yml`'s `static-analysis` job, documented in
`docs/ci-checks.md` and `README.md`.

The raw ruleset (`clang-analyzer-*` + `bugprone-*` + `cert-*` + `concurrency-*`
+ `performance-*` + `portability-*`) produced 635 findings on the first pass.
Almost all of them were one of two shapes: a check that does not fit this
codebase's conventions at all (documented as a `.clang-tidy` exclusion), or
`greatest.h`'s `ASSERT`/`FAIL` early-return idiom read as a leak/null/resource
defect by the analyzer's cross-function checkers (narrowed to production code
only, via an extra `--checks=` argument in `run.sh` for `*_test.c` files).

What was left after tuning was five real bugs, fixed rather than suppressed —
see `docs/static-analysis.md`'s "What the gate found on first run" for detail:
a leak on two error returns in `child_argv` (`src/kernel/workflow.c`), an
`ftell()`-failure integer-wraparound in `log_tail` (`src/kernel/summary.c`)
and `slurp` (`tools/bcembed.c`) that could reach a buffer-underflow write, an
unchecked `dup2(-1, ...)` in the OOM-test harness (`src/kernel/oom_shim.c`),
and two `qsort()` calls passed a possibly-null base pointer when the count is
zero (`src/kernel/jobs.c`, `src/kernel/validate.c`). One single-line false
positive (`src/kernel/workflow.c`'s `read_file`) is a `NOLINTNEXTLINE` with a
comment, not a `.clang-tidy` exclusion, since it is provably safe only for
that one loop, not the check as a whole.

No findings remain: `tools/clang-tidy/run.sh` exits 0 on the tree as committed.
