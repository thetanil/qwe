# 03: Summary hardening: log tails, escaping, append, write errors

Status: resolved
Category: enhancement
Type: task
Blocked by: 02

## What

Make the run summary safe and useful whatever a step prints:

- **Log tail.** Each failed or cancelled step gets a collapsed `<details>` block below its job's steps
  table, with the last 20 lines of that job's log. Those lines are already redacted, exactly as they are in
  the log. They sit inside a code fence longer than the longest backtick run they contain.
- **Escaping.** Table cells escape `|` and replace newlines with spaces. A cell longer than 200
  characters is cut, with `…` appended.
- **Redaction.** Nothing in the report bypasses secret redaction: not step ids, not reasons, not log
  lines.
- **Append.** Two `qwe run --summary F` calls leave both reports in F, in order.
- **Write failure.** If the file cannot be opened or written, qwe prints
  `qwe run: cannot write summary <file>: <reason>` to stderr. The run's exit status stays what the run
  decided.

## Acceptance criteria

- [x] A failing `run:` step's last 20 log lines appear in a `<details>` block, and earlier lines do not: `e2e: tests/e2e/summary_log_tail/`
- [x] A step whose output contains ```` ``` ````, `|` and a 300-character line renders with a longer fence, escaped cells and a cut cell: `e2e: tests/e2e/summary_markdown_hostile/`
- [x] A secret printed by a failing step shows as `***` in the tail and nowhere in plaintext in summary.md (check.sh greps for the plaintext): `e2e: tests/e2e/summary_secret_redacted/`
- [x] Two runs append to one file: `e2e: tests/e2e/summary_append/` (check.sh runs `$QWE_BIN` a second time)
- [x] A summary path in a missing directory: stderr message as above; the exit is 0 for a successful run, and 1 for a failing one: `e2e: tests/e2e/summary_unwritable/`, `e2e: tests/e2e/summary_unwritable_failed_run/` (both built during 02, already covering this)
- [x] `bazel test //...` green; the coverage floor holds

## Comments

- Log tail: `qwe_summary_write` gained a `run_dir` parameter. For each job with at least one
  `failed`/`cancelled` step, it reads `<run_dir>/<job-id>.log` (the file the run already wrote
  through the redactor — see `drain()` in `workflow.c`) and keeps the last 20 lines by walking
  back from the end counting newlines. Because that file is the *already-redacted* log, the
  tail needs no separate redaction pass — reading the right source was the whole trick. A job
  whose tail is empty, or whose log cannot be opened, gets no `<details>` block instead of an
  empty one.
- The fence is `longest_backtick_run(tail) + 1` backticks (minimum 3), so it always outruns
  whatever the step printed.
- Escaping: `escape_cell()` replaces `|` with `\|` and `\n` with a space, then cuts the
  *escaped* result to 200 bytes with `…` appended if it was longer. It is applied to every
  free-text table cell (job/step id or name fallback, plugin, reason); outcome markers and
  the log tail itself are left alone (the tail is raw, inside its own fence, by design).
  Reasons are always short fixed vocabulary strings (`exit-code`, `timeout`, ...), never raw
  step output, so the interesting hostile input is really in a step's `id:`/`name:` — `id:` is
  schema-restricted to `[A-Za-z_][A-Za-z0-9_-]*`, so `summary_markdown_hostile` uses `name:`
  for the pipe/long-cell cases.
- Regenerated `summary_failure_skip`'s and `summary_timeout`'s goldens from 02: both have a
  failed step, so they now legitimately gain (or, for the empty-log case, correctly don't gain)
  a log-tail block.
- `src/kernel/summary.c` needed a `tools/coverage/floor.txt` entry (6): the remaining uncovered
  lines are `asprintf`/`malloc`/short-`fread` failure guards, which is the same class of
  defensive-but-untestable-without-fault-injection line every other kernel file's floor entry
  already accounts for.
- `bazel test //...`: 224 passed, 3 skipped (pre-existing ssh-dependent skips), 0 failed.
  Coverage floor holds.
