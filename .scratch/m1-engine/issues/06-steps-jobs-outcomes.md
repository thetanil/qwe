# 06: Step sequencing, `needs:`, outcomes and reasons

Status: ready-for-agent
Type: task
Blocked by: 05

## What

Implement the job and step state machines described in the spec:
- Steps run in order within a job.
- `continue-on-error` applies per step.
- `needs:` uses the `default` join rule. Dependents of a failed job are `skipped` with reason `dependency-failed`.
- Jobs run **one at a time** in this ticket. Parallelism comes in ticket 08.
- `result.json` records outcome, reason and `changed` for every job and step.
- Exit codes are as in the spec.

## Acceptance criteria

- [ ] Three steps run in order, and the log shows them in order. `e2e: tests/e2e/steps_in_order/`
- [ ] A failing step stops the job: later steps don't run and don't appear in `result.json` as run. The job is `failed` and qwe exits 1. `e2e: tests/e2e/step_failure_stops_job/`
- [ ] A failing step with `continue-on-error: true` is recorded as `failed`, the next step runs, and the job is `success`. `e2e: tests/e2e/continue_on_error/`
- [ ] For A → B → C where A fails: B and C are `skipped` with reason `dependency-failed`, and an unrelated job D still runs. `e2e: tests/e2e/needs_skip_chain/`
- [ ] A workflow whose jobs are all `success` or `skipped` exits 0. `e2e: tests/e2e/exit_zero_with_skips/`
- [ ] The job state machine only makes the transitions listed in the spec. Every illegal transition asserts. `unit: src/kernel/job_state_test.c::legal_transitions`, `::illegal_transition_asserts`
