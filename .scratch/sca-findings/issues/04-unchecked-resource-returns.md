# 04: Unchecked resource and syscall returns

Status: ready-for-agent
Category: bug
Type: task
Blocked by: 01

## What

`cert-err33-c` is excluded outright because its default list covers most of
`<stdio.h>`, and 166 of its 321 hits are `fprintf`/`fputs`/`fputc` to
diagnostic streams (those are left to the next feature). The check has a
`CheckedFunctions` option. Re-enable it with that option set to the calls
whose failure means lost data or a wrong result:

| Call | Hits | Where (prod unless `_test`) |
|---|---|---|
| `fclose` | 37 | `tools/bcembed.c` (6), `src/kernel/summary.c` (4), `src/kernel/workflow.c` (2), many tests |
| `clock_gettime` | 6 | `luaexec.c` (3), `trace.c` (2), `workflow.c` |
| `fseek` | 5 | `tools/bcembed.c` (2), `summary.c`, 2 tests |
| `fflush` | 3 | `workflow.c` (2), `preamble_test.c` |
| `fwrite` | 2 | tests |
| `gmtime_r`, `strftime` | 4 | `result.c`, `workflow.c` |
| `timerfd_settime` | 1 | `timer.c` |
| `signal` | 1 | `summary_test.c` |

A failed `fclose` on a file that was written can lose data. That matters in
`summary.c` and `workflow.c`, and in `bcembed.c`, where a truncated
`embedded.h` would still compile. On a read-only stream, an explicit `(void)`
cast is fine, but only where a comment says the stream was read-only.

The `CheckedFunctions` list is fully qualified (`::fclose;::fflush;...`). Check
its syntax against `clang-tidy-20 --dump-config`. Ticket 05 appends to the same list.

## Acceptance criteria

- [ ] `.clang-tidy` enables `cert-err33-c` with `CheckedFunctions` set to exactly the calls above, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] A failing `fclose`/`fflush` on a written file makes `bcembed` and the summary/run-directory writers report an error, not succeed silently. `unit: <name the test added, e.g. an OOM/fault-injection case alongside the existing --wrap harness>`
- [ ] The `cert-err33-c` row in `docs/static-analysis.md` becomes "narrowed to …" with the reason, and real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
