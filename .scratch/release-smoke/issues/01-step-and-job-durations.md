# 01: Step and job durations in result.json

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

Each job and step in `result.json` gets a `duration_ms` field, taken from the monotonic clock. The existing
`started`/`ended` fields stay whole-second wall-clock times, and no existing field changes. A step that
never ran (`skipped`) has `duration_ms: null`, as its times are null today. A job's duration runs from when
it gets its slot to its outcome. A step's runs from when it starts running to when its process group is
empty (or its outcome, if it never got that far).

This is the prefactor for the run summary (02) and the performance job (15). Both read these numbers,
so the summary, `result.json` and the performance report can never disagree.

The e2e harness already replaces `started`/`ended` with `TIME`. Extend that to `duration_ms`
(`"DURATION"`), so no existing golden changes except for the new field.

## Acceptance criteria

- [x] `duration_ms` is present for every job and step that started, and is an integer ≥ 0: `e2e: tests/e2e/result_duration_ms/` (check.sh reads the raw result.json with jq)
- [x] A step with `sleep 1` has a step `duration_ms` in [1000, 3000], and its job's is ≥ the step's: `e2e: tests/e2e/result_duration_ms/`
- [x] A skipped job and its steps have `duration_ms: null`: `e2e: tests/e2e/result_duration_ms_skipped/`
- [x] The harness masks `duration_ms`, and every existing result.json golden gains the masked field and nothing else: `e2e: tests/e2e/file_ensure_idempotent/` (and the rest, via `bazel test //...`)
- [x] The result writer handles the new field: `unit: src/kernel/result_test.c::duration_written`
- [x] `bazel test //...` green

## Comments

- `result.json` gains `duration_ms` on every job and step, taken from `CLOCK_MONOTONIC`
  (stored as nanosecond counters on `struct job`/`struct job_run`, since `struct timespec`
  is not visible under the repo's `-std=c99` build without a POSIX feature macro in a
  shared header). `-1` is the "never started" sentinel internally; the writer turns that
  into JSON `null`, matching the existing `started`/`ended` convention.
- A job's duration is captured in `job_start`/`job_end`. A step's is captured in
  `step_finish` (when its process group empties) but only if nothing set it already —
  which covers a step that fails to spawn at all (`start-failed`, no process group ever
  existed): its duration is recorded at the same moment its outcome is, via `job_end`'s
  call into `step_finish`.
- `tests/e2e/run_case.sh` now also keeps an unmasked copy of `result.json` as
  `result.raw.json` (mirroring the existing `lifecycle.raw` pattern) so `check.sh` can
  assert on real millisecond values with `jq`, and masks `"duration_ms": <digits>` to
  `DURATION` in the golden copy (leaving `null` alone, like `started`/`ended`).
- All 21 existing `result.json` goldens were mechanically updated (awk script inserting
  `"duration_ms": DURATION` or `"duration_ms": null` right after each `"ended"` field,
  matching indentation and comma placement) — no other field changed.
- `bazel test //...`: 214 passed, 3 skipped (pre-existing ssh-dependent skips), 0 failed.
