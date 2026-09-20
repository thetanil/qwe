# 05: Measure coverage, then close the gaps that matter

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

There is no coverage measurement, so every statement about what is tested here
is an inference from file names. The suite is substantial — 139 test targets
and 110 e2e cases as of 2026-09-20 — which makes it more likely, not less, that
the untested parts are invisible.

**First, measure.** `bazel coverage //...` with gcov or llvm-cov, reported per
file, over the unit tests and the e2e cases together. The e2e cases drive the
real binary, so they cover a great deal that no `cc_test` touches, and a report
that leaves them out would send the work in the wrong direction.

**Then close what matters.** From reading, these files have no direct unit test
and are reached only end-to-end, which means their error paths are almost
certainly unexercised:

| File | What is untested |
|---|---|
| `src/kernel/sink.c` | line splitting, the unfinished last line, a failed log write |
| `src/kernel/result.c` | JSON escaping of control characters and quotes in ids and reasons |
| `src/kernel/validate.c` | the C position-lookup layer: pointer-to-position fallback to an ancestor, ordering of errors |
| `src/kernel/jobs.c` | loading malformed job tables, the timeout conversion |
| `src/kernel/trace.c` | line truncation at 512 bytes, the abort hook |
| `src/kernel/luaexec.c` | the poll loop: a command that exits without reading stdin, a partial write |
| `src/kernel/luafs.c` | a directory it cannot read |
| `src/kernel/luasecrets.c` | `reveal` on a malformed envelope, a missing key |
| `src/cli/run/run.c` | option parsing: `--job` with no value, `-i` twice, a second file, options after the file |

The CLI parsers are worth singling out. `qwe_cmd_run` and `qwe_cmd_validate`
grew an option each in the last three tickets, and their behaviour on malformed
input is asserted nowhere — `cli_unknown_subcommand` is the only case, and it
tests the dispatcher rather than the parsers.

**Do not chase a number.** A coverage percentage is a poor target and a good
diagnostic. The useful output of this ticket is a list of *specific*
unexercised branches that matter, each turned into a test or consciously
dismissed. Error paths and the code that runs when something has already gone
wrong are where the value is, because that is exactly the code no happy-path
e2e case reaches.

If certification later names a coverage criterion — statement, branch, MC/DC —
that will decide the target. Until then the honest goal is "every error path we
can reach from a test has one".

## Acceptance criteria

- [x] `bazel coverage //...` produces a per-file report over the unit tests and the e2e cases, and the command to produce it is written down where a newcomer will find it.
- [x] The baseline report is committed or its summary recorded in this ticket, so later change is measurable against it.
- [x] Each file in the table above either gains a unit test for its error paths, or gets a line in this ticket saying why it is adequately covered end-to-end.
- [x] CLI option parsing is covered directly: `--job` with no value, a repeated `-i`, two workflow files, and an unknown option each produce usage and exit 2. `unit: src/cli/run/run_test.c::option_parsing`
- [x] `result.c` escapes correctly: a reason or id containing a quote, a backslash, a newline and a control character round-trips as valid JSON. `unit: src/kernel/result_test.c::escaping`
- [x] `sink.c` splits lines correctly: output with no trailing newline, a lone newline, and a line longer than the initial buffer. `unit: src/kernel/sink_test.c::line_splitting`
- [x] `trace.c` truncates a long line without corrupting the file. `unit: src/kernel/trace_test.c::long_line_is_truncated`

## Comments

### Resolved

Commands, how to read the report, and the before/after per-file numbers are in `docs/coverage.md`. `bazel test //...` is green (174 pass, 3 skipped as before).

**Measured.** `bazel coverage //... --combined_report=lcov`, about 35 s, unit tests and e2e cases in one report (the e2e cases run the real binary and their coverage is collected). Baseline 74.2% of lines (3404/4587); after 75.5% (3463/4588). No branch data comes out of this toolchain. The percentage was not the goal, the uncovered lines were.

**Found a real bug.** `trace_test::long_line_is_truncated` was red first: a record longer than 511 bytes was cut *through its trailing newline*, so the next record was glued onto the same line and the trace file lost a record boundary. Fixed in `trace.c` (a cut line still ends in `\n`).

**Found, not fixed.** `sink.c`'s `write_all` returns silently when the log write fails (ENOSPC, EIO), so log bytes can be lost with no report; the terminal still gets its lines. `sink_test::failed_log_write_does_not_stop_the_terminal` pins "no crash, no hang, terminal unaffected" only. Whether a failed log write should fail the job is a decision for its own ticket, since the log is the source of truth (`alloc.h`, rule 2).

**The table, file by file:**

| File | Outcome |
|---|---|
| `cli/run/run.c` | `run_test::option_parsing` extended: `-i` with no value, `-i` twice, an unknown option after the file. The two uncovered lines are the calloc-failure path (ticket 07). |
| `cli/validate/validate.c` | had no unit test; new `validate_test::option_parsing`, 16/16. |
| `kernel/result.c` | new `result_test`: `escaping` and `hostile_text_in_a_result`, 78/78. Expected strings are RFC 8259 worked by hand. |
| `kernel/sink.c` | new `sink_test`: `line_splitting` (no trailing newline, lone newline, a 5000-byte line across writes, the log stays raw), an empty sink, a failed log write. The two uncovered lines are the EINTR retry and an allocation failure (ticket 07). |
| `kernel/trace.c` | new `trace_test::long_line_is_truncated` (found the bug above). Still uncovered: the open-failure and abort-hook lines, and the `strerrorname_np` fallback table, which is compiled out on this libc and tested by `errno_name_test` in its own build. |
| `kernel/luaexec.c` | added to `luarun_test.sh`: a command that never reads stdin, 1 MiB stdin through a 64K pipe (partial writes), a command that cannot be exec'd, a timeout, a signalled command. Verified live by breaking an expectation. Remaining lines are allocation and pipe/fork failure paths. |
| `kernel/luafs.c` | added to `luarun_test.sh`: `list` on a missing path, a file and an unreadable directory; `private_dir` under a file and on a file. Remaining lines are allocation failures. |
| `kernel/luasecrets.c` | added to `luarun_test.sh`: `reveal` with no `value`, with no key file, and with four malformed envelopes given a real key. |
| `kernel/jobs.c` | **dismissed.** The uncovered lines are `unimplemented_job_keys` / `unimplemented_step_keys` (both `{NULL}`, so the loops are dead until a key is listed), the bad-job return, and the "a step needs run: or uses:" refusal. That last one is shadowed by the schema, which refuses the same workflow first with a position (`w.yaml:5:9: a step needs either run: or uses:`, checked by hand). The timeout conversion is covered end-to-end by `step_timeout_fails` and `validate_timeout_*`. |
| `kernel/validate.c` | **dismissed, with one added case.** The uncovered lines are the ancestor fallback in `locate`, `escape_token`'s `~` and `/` branches, and the internal-error paths. `escape_token` cannot see a `/` or `~`: the schema refuses job ids that contain them first, and a DAG error is reported only when the schema is clean, so neither is reachable from a workflow. Added `e2e: validate_errors_in_position_order`, which pins that errors come out in file order although Lua's `pairs` yields them in hash order. |

Still the two largest gaps in `src/`, outside this table: `cli/encrypt/encrypt.c` (5/50) and `kernel/proc.c` (34/59).
