# 02: qwe run --summary: a markdown report of the run

Status: resolved
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

- [x] A successful two-job run with a changed and an unchanged `file.ensure` step writes the heading, the jobs table and both steps tables: `e2e: tests/e2e/summary_success/` (golden `expected/summary.md`, with durations masked)
- [x] A failed job and its `dependency-failed` dependant: `❌ failed` and `⏭️ skipped` with reasons, and the skipped job has no steps table: `e2e: tests/e2e/summary_failure_skip/`
- [x] A step timeout shows `failed` / `timeout`: `e2e: tests/e2e/summary_timeout/`
- [x] Without `--summary`, no file is written and output is unchanged: `e2e: tests/e2e/file_ensure_idempotent/` (unchanged golden)
- [x] `--summary` with no argument prints usage and exits 2: `unit: src/cli/run/run_test.c::summary_needs_argument`
- [x] The harness masks the duration column in `summary.md` goldens (documented at the top of the harness script): `e2e: tests/e2e/summary_success/`
- [x] `bazel test //...` green; the coverage floor holds (`bazel run //tools/coverage:check`)

## Comments

- New `src/kernel/summary.c`/`summary.h`: `qwe_summary_write()` builds the report straight
  from the same `qwe_job_result`/`qwe_step_result` arrays `result.json` is written from, so
  the two can never disagree. It opens the file in append mode, so repeated `--summary` runs
  already accumulate (03 hardens this further with a dedicated e2e test).
- `qwe_step_result` gained two fields the JSON writer does not serialize: `name` (the step's
  `name:`, for the id column's fallback) and `plugin` (`"run"`, or the step's `uses:` value).
  Both are populated the same way `id` already was, in `step_spawn` and in `job_end`'s
  trailing-skipped-steps loop, and freed in `qwe_jobs_free` alongside `id`.
- `--job` is joined by `--summary <file>` in `run.c`'s option parser (usable anywhere among
  the run options, same as `-i`); `struct qwe_run_options` gained a `summary` field.
- The overall run outcome/duration for the heading is computed from the same `rc`/`all_ok`
  logic that decides the process exit code, and a monotonic wall-clock duration taken around
  `run_all()` (not the whole `qwe_run_workflow`, so parsing/loading is excluded).
- Coverage forced two additional e2e cases ahead of ticket 03's schedule:
  `summary_unwritable` and `summary_unwritable_failed_run`, both asserting the
  `qwe run: cannot write summary <file>: <reason>` stderr line and that it never touches the
  run's own exit status. Ticket 03 can build on these rather than re-adding them.
- `bazel test //...`: 220 passed, 3 skipped (pre-existing ssh-dependent skips), 0 failed.
  Coverage floor holds (`bazel run //tools/coverage:check`).
