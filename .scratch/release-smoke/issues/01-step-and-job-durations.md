# 01: Step and job durations in result.json

Status: ready-for-agent
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

- [ ] `duration_ms` is present for every job and step that started, and is an integer ≥ 0: `e2e: tests/e2e/result_duration_ms/` (check.sh reads the raw result.json with jq)
- [ ] A step with `sleep 1` has a step `duration_ms` in [1000, 3000], and its job's is ≥ the step's: `e2e: tests/e2e/result_duration_ms/`
- [ ] A skipped job and its steps have `duration_ms: null`: `e2e: tests/e2e/result_duration_ms_skipped/`
- [ ] The harness masks `duration_ms`, and every existing result.json golden gains the masked field and nothing else: `e2e: tests/e2e/file_ensure_idempotent/` (and the rest, via `bazel test //...`)
- [ ] The result writer handles the new field: `unit: src/kernel/result_test.c::duration_written`
- [ ] `bazel test //...` green

## Comments
