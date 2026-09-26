# 06: `concurrency-mt-unsafe`

Status: resolved
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

- [x] `.clang-tidy` enables `concurrency-mt-unsafe` (narrowed as needed), and the gate exits 0; or the ticket is `wontfix` with the evidence in its comments. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] No `strerror` in `src/` or `tools/` outside the helper, if one is added. `manual: grep -rnw strerror src tools`
- [x] "qwe has no threads" is checked by a test, or the comments say why it can't be. `unit: <the test>` or `manual: this ticket's ## Comments`
- [x] The doc row is removed, or rewritten to state the narrowing and what enforces it. `manual: docs/static-analysis.md`
- [x] `bazel test //...` and the coverage check are green. `unit: bazel test //...`; `manual: bazel run //tools/coverage:check`

## Comments

Worked through all three steps; the check is enabled, not `wontfix`.

1. **Narrowed** with `concurrency-mt-unsafe.FunctionSet: glibc`: 49 findings
   (matches the ticket): `strerror` 20 (`workflow.c` 9, `keyfile.c` 4, `luafs.c` 3,
   `luaexec.c` 1, `bcembed.c` 1, `luaexec_test.c` 2), `sigprocmask` 12 (production
   4, `proc_test.c` 8), `setenv`/`unsetenv` 14 in tests, `sleep` 1
   (`oom_shim_test.c`), `exit` 1 (`luavm.c`'s panic handler).
2. **Fixed:**
   - `qwe_strerror` (`src/kernel/errstr.{h,c}`, unit test `errstr_test`): XSI
     `strerror_r` into a `__thread` buffer, with `errno` saved and restored (the
     caller reads it for the next message) and glibc's own "Unknown error N" for
     an errno it does not know, so the diagnostics are unchanged. Not
     `strerrordesc_np`: `trace.c` already carries a fallback for libcs without
     the `_np` functions, and this stays portable the same way.
     `grep -rnw strerror src tools` finds only `errstr.c`'s `strerror_r`.
   - `sigprocmask` → `pthread_sigmask`: same call on one thread, and correct on
     more. It needs nothing linked in glibc 2.39, and `no_threads_test` below
     confirms it does not pull in `pthread_create`.
   - `sleep` → `nanosleep`.
   - `setenv`/`unsetenv` in tests → `qwe_test_setenv`/`qwe_test_unsetenv`
     (`src/testing/env.h`), each with a `NOLINTNEXTLINE(concurrency-mt-unsafe)`
     saying why. Eliminating the environment writes was not an option: the tests
     exist to set `QWE_LUA_COVERAGE`, `HOME` and the OOM shim's variables for the
     code under test to read.
   - `luavm.c`'s `exit(1)`: `NOLINTNEXTLINE`, with a comment. `_exit` would skip
     the stdio and gcov flush that a normal exit does.
3. **Enforced the premise:** `//src/cli:no_threads_test`
   (`no_threads_test.sh`) fails if `qwe-debug` (static, unstripped) references
   `pthread_create`, `thrd_create` or `__pthread_create_2_1`. In glibc every
   thread, including `timer_create`'s `SIGEV_THREAD` and aio, starts there.
   `clone`/`clone3` are in the binary from `fork` and cannot be the test. The
   script also fails if it cannot read `main` from the binary, and if it does
   not find `pthread_create` in `//src/cli:threads_probe`, a testonly program
   that does start a thread, so a blind check cannot pass. Checked by hand:
   pointing the script at the probe as if it were qwe exits 1 naming
   `pthread_create`.

Negative check: `const char *qwe_tmp_probe(int e) { return strerror(e); }`
appended to `src/kernel/clock_test.c` failed the gate with
`function is not thread safe [concurrency-mt-unsafe]`. Reverted.

Gate exit 0; `bazel test //...` 260 pass, 3 skipped; coverage check: every file
at least 85%.
