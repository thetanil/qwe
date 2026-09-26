# 08: `rand` in `sched_test`

Status: ready-for-agent
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

- [ ] No `rand`/`srand` in `src/` or `tools/`. `manual: grep -rnwE 'rand|srand' src tools`
- [ ] `sched_test` still passes and runs the same number of cases. `unit: src/kernel/sched_test.c`
- [ ] `.clang-tidy` excludes none of the four checks, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] The doc row is gone. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
