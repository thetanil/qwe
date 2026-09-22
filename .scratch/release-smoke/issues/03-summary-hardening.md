# 03: Summary hardening: log tails, escaping, append, write errors

Status: ready-for-agent
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

- [ ] A failing `run:` step's last 20 log lines appear in a `<details>` block, and earlier lines do not: `e2e: tests/e2e/summary_log_tail/`
- [ ] A step whose output contains ```` ``` ````, `|` and a 300-character line renders with a longer fence, escaped cells and a cut cell: `e2e: tests/e2e/summary_markdown_hostile/`
- [ ] A secret printed by a failing step shows as `***` in the tail and nowhere in plaintext in summary.md (check.sh greps for the plaintext): `e2e: tests/e2e/summary_secret_redacted/`
- [ ] Two runs append to one file: `e2e: tests/e2e/summary_append/` (check.sh runs `$QWE_BIN` a second time)
- [ ] A summary path in a missing directory: stderr message as above; the exit is 0 for a successful run, and 1 for a failing one: `e2e: tests/e2e/summary_unwritable/`, `e2e: tests/e2e/summary_unwritable_failed_run/`
- [ ] `bazel test //...` green; the coverage floor holds

## Comments
