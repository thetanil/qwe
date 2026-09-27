# 22: Cognitive complexity

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 02, 10, 19, 20, 21

## What

`readability-function-cognitive-complexity` (threshold 25, its default) reports 13
functions in `src/` and `tools/` and 122 findings in tests
(2026-09-26, `bb9d69f`):

| Function | File | Complexity |
|---|---|---|
| `exec_run` | `luaexec.c:50` | 108 |
| `jobs.c` `qwe_jobs_load` | `jobs.c:56` | 56 |
| `encode_table` | `luacbor.c:228` | 56 |
| `qwe_summary_render` | `summary.c:152` | 49 |
| `qwe_run_workflow` | `workflow.c:1788` | 49 |
| `check_dag` | `validate.c:122` | 47 |
| `run_all` | `workflow.c:1593` | 41 |
| `main` | `bcembed.c:67` | 34 |
| `report_disabled` | `workflow.c:1751` | 31 |
| `qwe_key_generate` | `keyfile.c:102` | 29 |
| `handle_event` | `transcode.c:281` | 29 |
| `read_step_msg` | `workflow.c:1263` | 28 |
| `exec_preamble` | `luaexec.c:318` | 26 |

A function with a complexity of 108 has no test set that reaches every path,
which is what the "85% per file" rule cannot see. It is also where the bugs of
this session lived: `exec_run` (ticket 02), `report_disabled` (a memstream
bug in round 2), `qwe_summary_render` (round 2's `ferror` bug),
`qwe_run_workflow`.

## Fix

Split each into steps with a name, an input struct, and one job. Do it after the
functions' other tickets (02, 10, 19, 20, 21), or in the same change, so the
rewrite is done once. A threshold is a number the assessor will accept once: use
the check's default of 25 and do not raise it. Add the check to `.clang-tidy`
with no `CheckOptions`.

Tests: the existing tests must keep passing unchanged. Where a split creates
a function whose error branch is reachable by a test that the old shape could
not reach, add that test.

## Acceptance criteria

- [ ] `readability-function-cognitive-complexity` is on at its default threshold
      and the gate exits 0 for `src/` and `tools/`. `manual: run.sh`
- [ ] Tests: the 122 findings are fixed too, the same way; the check is not
      limited to non-test code (tests get the same checks, as in ticket 12, and
      a path restriction is a `narrow` option under ticket 28). The two shapes:
      - `main`s: greatest's `RUN_TEST` is a macro that expands to a branch, and
        the check counts macro expansions (its `IgnoreMacros` option would be a
        narrowing, so it stays off). Group the tests into greatest `SUITE`s of a
        handful each and have `main` call `RUN_SUITE`, which is greatest's own
        structure; the suite and test counts stay the same.
      - table-driven or long test bodies: split into a named helper per case
        shape, as in `src/`.

      `manual: gate at exit 0 for tests too; the Comments give the test count
      before and after, which must match`
- [ ] No behaviour change: same e2e goldens, same unit tests.
      `manual: bazel test //...`
- [ ] The coverage check is green. Coverage of every new function is at least
      85%.

## Comments
