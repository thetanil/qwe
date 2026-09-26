# 06: `concurrency-mt-unsafe`

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

`concurrency-mt-unsafe` is excluded because "qwe has no threads" (ADR-0001:
one process per plugin step). There are 79 raw findings. In production code
(33): `strerror` 18, `getenv` 9, `sigprocmask` 4, `readdir`, `exit`. In tests
(46): `getenv` 12, `setenv` 9, `sigprocmask` 8, `unsetenv` 6, `rand` 5,
`readdir` 3, `strerror` 2, `sleep`.

The claim behind the exclusion is not enforced anywhere. If a thread is ever
added (a libsodium or LuaJIT option, a future plugin runner), all 79 sites
become real at once, and the check will have been off the whole time.

Work through it in this order, and stop where the evidence says to:

1. **Narrow.** `FunctionSet: glibc` (the option's default is `any`) flags only
   what is thread-unsafe on glibc, which qwe targets. At `2d56de5` that leaves
   49 (`workflow.c` 11, `proc_test.c` 8, `oom_shim_test.c` 8, `keyfile.c` 4,
   `gcov_test.c` 4, `luafs.c` 3, and one or two in 8 more files).
2. **Fix the cheap ones in production code.** For `strerror`, a
   `qwe_strerror` that uses `strerror_r` into a caller buffer (or glibc's
   `strerrordesc_np`) is one helper. Check what the remaining `getenv` calls
   actually need before choosing a pattern.
3. **Enforce the premise for what is left.** For example, a test that fails
   if the linked `qwe` binary references `pthread_create`/`clone3` with
   `CLONE_THREAD`, or a check in `run.sh`. If the rest (`setenv`/`unsetenv` in
   tests, `sigprocmask`) is fine because of that premise, the exclusion can stay
   narrowed or scoped to tests, and now it rests on something tested.

If step 3 isn't feasible, close `wontfix`, record the evidence, and keep the
doc row, now quoting the measured numbers.

## Acceptance criteria

- [ ] `.clang-tidy` enables `concurrency-mt-unsafe` (narrowed as needed), and the gate exits 0; or the ticket is `wontfix` with the evidence in its comments. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] No `strerror` in `src/` or `tools/` outside the helper, if one is added. `manual: grep -rnw strerror src tools`
- [ ] "qwe has no threads" is checked by a test, or the comments say why it can't be. `unit: <the test>` or `manual: this ticket's ## Comments`
- [ ] The doc row is removed, or rewritten to state the narrowing and what enforces it. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` and the coverage check are green. `unit: bazel test //...`; `manual: bazel run //tools/coverage:check`

## Comments
