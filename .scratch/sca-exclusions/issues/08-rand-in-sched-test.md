# 08: `rand` in `sched_test`

Status: resolved
Category: enhancement
Type: task

## What

`cert-msc30-c`, `cert-msc32-c`, `cert-msc50-cpp` and `cert-msc51-cpp` are
excluded for `rand()`. All six findings are in `src/kernel/sched_test.c`:
`rand()` at lines 20, 28, 58, 76 and 77, and `srand(20260919)` at line 55.

The doc's reason ("test-only jitter; the real crypto is libsodium's") is true,
but a small local PRNG fixes it better than the exclusion does. For example, a
`static uint32_t` xorshift32 seeded with the same constant. The sequence then
no longer depends on the libc's `rand`, so a failure found on one libc
reproduces on another. Print the seed on failure if the test does not already.

Then remove all four checks from the exclusion list. The two `-cpp` aliases
never fire on C, but excluding them does nothing either.

## Acceptance criteria

- [x] No `rand`/`srand` in `src/` or `tools/`. `manual: grep -rnwE 'rand|srand' src tools`
- [x] `sched_test` still passes and runs the same number of cases. `unit: src/kernel/sched_test.c`
- [x] `.clang-tidy` excludes none of the four checks, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] The doc row is gone. `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

`sched_test.c` has its own xorshift32 (`rng_next`, `rng_below`), seeded with
the same constant, `20260919`. The graphs it draws differ from what libc's `rand`
gave (a different generator), but they are now the same on every libc. The test
still has the same cases (`never_exceeds_max_parallel` still runs 2000 trials
and asserts the same two things) and passes.

A failure names the seed, the trial and the generator state that trial started
from (`seed 20260919, trial 0, generator state 20260919 at its start`), through
`ASSERTm`. Verified by temporarily tightening the progress bound to make it
fail. The first attempt printed garbage: the message buffer was a local of
the test, and greatest reads it after the test has returned (the doc records the
same trap for `lifecycle_test.c`). It is a file-scope static now, with a
comment.

`grep -rnwE 'rand|srand' src tools` finds only a comment in `sched_test.c`.
The four checks are out of `.clang-tidy` (its `Checks` now ends with
`-bugprone-easily-swappable-parameters`), gate exit 0, `bazel test //...` 260
pass and 3 skipped, coverage check green.
