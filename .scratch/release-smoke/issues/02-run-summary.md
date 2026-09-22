# 02: qwe run --summary: a markdown report of the run

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

`qwe run <workflow> --summary <file>` appends a markdown report of the run to `<file>`, once, at the end of
the run, from the same result data as `result.json`. In CI the file is `$GITHUB_STEP_SUMMARY`. The option
may appear anywhere among the run options. Without it, nothing changes.

The report contains:

- a level-3 heading with the workflow file name, the overall outcome and the total duration;
- a jobs table: job, outcome, reason, duration (ms);
- one steps table per job that started: index, id (or `name:`, or blank), plugin (`run`, or the
  `uses:` value), outcome, changed/unchanged, reason, duration (ms).

The changed column says `changed` for a `success` with changed true, `unchanged` for a `success` with
changed false (check found nothing to do), and is blank otherwise. `skipped` is an outcome, shown as one,
and never mixed up with unchanged. Outcomes are text first (`✅ success`, `❌ failed`, `⏭️ skipped`,
`⏹️ cancelled`), so the table reads as plain text too. Reasons use the existing reason strings.

A failed or cancelled run still writes the report: failure is the case it matters most for. Log tails,
escaping and write errors are 03.

Update the README usage line and the `run` usage text.

## Acceptance criteria

- [ ] A successful two-job run with a changed and an unchanged `file.ensure` step writes the heading, the jobs table and both steps tables: `e2e: tests/e2e/summary_success/` (golden `expected/summary.md`, with durations masked)
- [ ] A failed job and its `dependency-failed` dependant: `❌ failed` and `⏭️ skipped` with reasons, and the skipped job has no steps table: `e2e: tests/e2e/summary_failure_skip/`
- [ ] A step timeout shows `failed` / `timeout`: `e2e: tests/e2e/summary_timeout/`
- [ ] Without `--summary`, no file is written and output is unchanged: `e2e: tests/e2e/file_ensure_idempotent/` (unchanged golden)
- [ ] `--summary` with no argument prints usage and exits 2: `unit: src/cli/run/run_test.c::summary_needs_argument`
- [ ] The harness masks the duration column in `summary.md` goldens (documented at the top of the harness script): `e2e: tests/e2e/summary_success/`
- [ ] `bazel test //...` green; the coverage floor holds (`bazel run //tools/coverage:check`)

## Comments
