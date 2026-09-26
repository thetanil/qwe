# 03: Analyzer checks in test code

Status: resolved
Category: bug
Type: task
Blocked by: 01

## What

`run.sh` turns off five analyzer checkers for `*_test.c`, on the grounds that
greatest's `ASSERT`/`FAIL` early return always looks like a leak. The raw
run finds 30 hits across those five:

| Checker | Hits | Sites |
|---|---|---|
| `unix.Malloc` | 15 | `summary_test.c` (8), `preamble_test.c` (3), `envelope_test.c` (2), `limits_test.c`, `alloc_test.c` |
| `core.NonNullParamChecker` | 8 | the `*oom_test.c` files, `preamble_test.c`, `summary_test.c`, `workflow_test.c` |
| `unix.Stream` | 3 | `lifecycle_test.c:449`, `proc_test.c:104`, `trace_test.c:36` |
| `unix.StdCLibraryFunctions` | 3 | `load_oom_test.c:62,68`, `preamble_test.c:32` |
| `optin.portability.UnixAPI` | 1 | `summary_test.c:24` |

Thirty sites is few enough to go through one at a time. Some will be real
test bugs, for example a null that reaches a libc call because a setup step's
return went unchecked. Others will be the early-return pattern. For those,
the usual greatest fixes are to allocate after the asserts, or to free
before asserting, or to use a per-suite teardown (`GREATEST_SET_TEARDOWN_CB`)
that owns the cleanup. Pick one idiom and use it everywhere. Do not
use `NOLINT` per site.

Then delete `test_only_checks` from `run.sh` and its paragraph from the doc.

## Acceptance criteria

- [x] `run.sh` applies the same checks to `*_test.c` as to every other file, and exits 0. `manual: grep -n test_only_checks tools/clang-tidy/run.sh finds nothing; CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] Each site's comment records whether it was a real bug or the early-return pattern, and names the idiom chosen. `manual: this ticket's ## Comments`
- [x] Tests still pass under valgrind and the sanitizers. `manual: the valgrind and sanitizer gates per docs/ci-checks.md`
- [x] `docs/static-analysis.md`'s test-narrowing paragraph is replaced by the chosen idiom, and real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

- Idioms chosen (documented in `docs/static-analysis.md`, "Test code: same checks, two idioms"):
  - **Early-return pattern: the per-test teardown owner.** New `src/testing/owned.h` (`//src/testing:owned`, testonly, header-only). `qwe_own(p)` / `qwe_own_file(fp)` register a pointer. `qwe_release_owned`, installed with `SET_TEARDOWN` at the top of `main` and of each `SUITE` (greatest clears it at suite end), frees and closes everything after every test, pass or fail. The owned pointer escapes to a file-scope array, so the analyzer no longer reads the early return as a leak. No `NOLINT` anywhere.
  - **Real bugs: setup helpers abort on failure.** A fixture helper that did not check `fopen`/`open`/`tmpfile`/`dup`/`ftell`/`malloc` now `abort()`s, the idiom `preamble_test.c`'s `run()` already used.
- Per site (line numbers from the first run with narrowing removed; 30 hits):

  | Site | Checker | Verdict | Fix |
  |---|---|---|---|
  | `cli/encrypt/oom_test.c:61` | NonNullParam | real: unchecked `fopen` of the key file into `fwrite` | abort |
  | `cli/validate/oom_test.c:26` | NonNullParam | real: `write_file` unchecked `fopen` into `fputs` | abort |
  | `kernel/load_oom_test.c:47` | NonNullParam | real: same `write_file` | abort |
  | `kernel/load_oom_test.c:62,68` | StdCLibraryFunctions | real: `validate_failing` passed unchecked `open` (and `dup(2)`) to `dup2` | abort |
  | `kernel/oom_test.c:26` | NonNullParam | real: `write_file` | abort |
  | `kernel/oom_test.c:241` | NonNullParam | real: `fresh_select_case` unchecked `fopen(..., "a")` | abort |
  | `kernel/workflow_test.c:32` | NonNullParam | real: `write_file` | abort |
  | `kernel/preamble_test.c:29` | NonNullParam | real: unchecked `tmpfile` into `fwrite` | abort |
  | `kernel/preamble_test.c:32` | StdCLibraryFunctions | real: unchecked `dup` into `lseek` | abort |
  | `kernel/preamble_test.c:94,109,114` | Malloc | early return: `all` freed after asserts | owner |
  | `kernel/summary_test.c:21` | NonNullParam | real: `slurp` unchecked `fopen` into `fseek` | abort |
  | `kernel/summary_test.c:24` | UnixAPI | real: `slurp` unchecked `ftell`, so `-1` became `malloc(0)` and then `buf[0]` was written past the block. The same bug the first run fixed in `summary.c` | abort (also on `malloc` NULL) |
  | `kernel/summary_test.c:62,78,96,117,139,143,186,193` | Malloc | early return: `out`/`buf` freed after asserts | owner |
  | `kernel/alloc_test.c:88` | Malloc | early return: `p` freed after `ASSERT_EQ_FMT` in the loop | owner |
  | `edge/yaml/limits_test.c:43` | Malloc | early return: `big` freed after asserts | owner |
  | `secrets/envelope_test.c:59,65` | Malloc | early return (`copy`), plus a real unchecked `strdup` that the loop wrote into | owner. `env` and a single `copy` buffer (refilled with `memcpy` each iteration, instead of ~100 `strdup`s) are owned; `copy`'s allocation is asserted |
  | `kernel/lifecycle_test.c:452` | Stream | early return: `fp` closed after `ASSERT(fgets(...))` | owner (`qwe_own_file`) |
  | `kernel/proc_test.c:104` | Stream | early return, same shape | owner |
  | `kernel/trace_test.c:36` | Stream | early return, same shape | owner |

  The 15 Malloc sites break down as summary 8, preamble 3, envelope 2, limits 1 and alloc 1; the spec's table is right. Also fixed, though the analyzer did not flag them: the two unchecked `fopen(logpath, "w")` in `summary_test.c`'s log-tail tests now have an `ASSERT(fp != NULL)`.
- The gate exits 0 with no `test_only_checks`. `bazel test //...`, `--config=asan //...` and `--config=ubsan //...` are all green.
- Valgrind (`--config=valgrind`) on the 13 touched test targets: all pass, including the three oom sweeps (`cli/validate:oom_test` 1583 s, `kernel:oom_test` 1256 s, `kernel:load_oom_test` 691 s, run concurrently).
