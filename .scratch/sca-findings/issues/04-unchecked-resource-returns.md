# 04: Unchecked resource and syscall returns

Status: resolved
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

- [x] `.clang-tidy` enables `cert-err33-c` with `CheckedFunctions` set to exactly the calls above, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] A failing `fclose`/`fflush` on a written file makes `bcembed` and the summary/run-directory writers report an error, not succeed silently. `unit: //tools:bcembed_test` (output to `/dev/full`); `summary_test`'s `write_failure_after_open_fails` (already there)
- [x] The `cert-err33-c` row in `docs/static-analysis.md` becomes "narrowed to …" with the reason, and real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

- `CheckedFunctions: '^::fclose$;^::fflush$;^::fwrite$;^::fseek$;^::clock_gettime$;^::gmtime_r$;^::strftime$;^::timerfd_settime$;^::signal$'`, under `CheckOptions:` in `.clang-tidy` (regexes, so each is anchored at both ends). 56 hits at `0eb7f1c`, not the 62 the table counts: ticket 03's fixture fixes had already removed six.
- Real bugs (listed in `docs/static-analysis.md`):
  - `result.json`: `!fp || write < 0 || fclose(fp) != 0` skipped the `fclose` after a failed write. It is now sequenced so the stream always closes.
  - `bcembed` never checked its output's final `fclose`, so a full disk left a truncated table that compiled. It now exits 1. Test: `//tools:bcembed_test` (red before the fix, green after).
  - `fseek` unchecked before `ftell` in `bcembed`'s `slurp`, `summary.c`'s `log_tail` and `corpus_test.c`'s `replay`.
  - `gmtime_r`/`strftime` unchecked in `result.c`'s `put_time` and `workflow.c`'s `fmt_run_id`: an unset buffer was printed. Both fall back to epoch seconds. `put_time` keeps it a JSON string. Test: `result_test`'s `time_past_gmtime_written_as_seconds` (red before, green after).
  - `report_disabled` printed the memstream buffer after an unchecked `fclose`. The buffer is only set on a clean close.
  - A plugin step's child exited 0 when the final `fflush(stdout)` failed. It now exits 1. On the plugin-error path it is already exiting 1, so the flush is `(void)` with that comment.
- Not bugs, but now honest: six `clock_gettime(CLOCK_MONOTONIC)` calls go through the new `src/kernel/clock.h` (`//src/kernel:clock`) `qwe_mono_now()`, which aborts on a failure Linux cannot produce. `qwe_timer_disarm` returns `int` like `qwe_timer_arm`.
- `(void)fclose` only on read-only streams (`read_file`, `log_tail`, `slurp`s, `proc_test`, `oom_test`/`workflow_test` readers), on already-failing error paths in `bcembed`, and in `owned.h`'s teardown (it runs after the test, so it cannot fail it), each with a comment saying which.
- Test fixtures follow ticket 03's idiom: a fixture writer aborts if `ferror` or `fclose` fails. A test's own written stream gets `ASSERT_EQ(0, fclose(fp))`.
- No test for a run-directory `result.json` write failure: the path holds a fresh run id, so it cannot be pointed at `/dev/full` without a seam in `qwe_run_workflow`. The summary writer's `fclose` failure was already covered.
- Gate: `CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh` exits 0. `bazel test //...` is green (249 pass, 3 skipped as before).
