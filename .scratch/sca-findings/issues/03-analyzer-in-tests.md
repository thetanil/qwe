# 03: Analyzer checks in test code

Status: ready-for-agent
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

- [ ] `run.sh` applies the same checks to `*_test.c` as to every other file, and exits 0. `manual: grep -n test_only_checks tools/clang-tidy/run.sh finds nothing; CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] Each site's comment records whether it was a real bug or the early-return pattern, and names the idiom chosen. `manual: this ticket's ## Comments`
- [ ] Tests still pass under valgrind and the sanitizers. `manual: the valgrind and sanitizer gates per docs/ci-checks.md`
- [ ] `docs/static-analysis.md`'s test-narrowing paragraph is replaced by the chosen idiom, and real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
