# 05: Measure coverage, then close the gaps that matter

Status: ready-for-agent
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

- [ ] `bazel coverage //...` produces a per-file report over the unit tests and the e2e cases, and the command to produce it is written down where a newcomer will find it.
- [ ] The baseline report is committed or its summary recorded in this ticket, so later change is measurable against it.
- [ ] Each file in the table above either gains a unit test for its error paths, or gets a line in this ticket saying why it is adequately covered end-to-end.
- [ ] CLI option parsing is covered directly: `--job` with no value, a repeated `-i`, two workflow files, and an unknown option each produce usage and exit 2. `unit: src/cli/run/run_test.c::option_parsing`
- [ ] `result.c` escapes correctly: a reason or id containing a quote, a backslash, a newline and a control character round-trips as valid JSON. `unit: src/kernel/result_test.c::escaping`
- [ ] `sink.c` splits lines correctly: output with no trailing newline, a lone newline, and a line longer than the initial buffer. `unit: src/kernel/sink_test.c::line_splitting`
- [ ] `trace.c` truncates a long line without corrupting the file. `unit: src/kernel/trace_test.c::long_line_is_truncated`

## Comments
