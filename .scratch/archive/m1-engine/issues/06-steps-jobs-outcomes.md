# 06: Step sequencing, `needs:`, outcomes and reasons

Status: resolved
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

- [x] Three steps run in order, and the log shows them in order. `e2e: tests/e2e/steps_in_order/`
- [x] A failing step stops the job: later steps don't run and don't appear in `result.json` as run. The job is `failed` and qwe exits 1. `e2e: tests/e2e/step_failure_stops_job/`
- [x] A failing step with `continue-on-error: true` is recorded as `failed`, the next step runs, and the job is `success`. `e2e: tests/e2e/continue_on_error/`
- [x] For A → B → C where A fails: B and C are `skipped` with reason `dependency-failed`, and an unrelated job D still runs. `e2e: tests/e2e/needs_skip_chain/`
- [x] A workflow whose jobs are all `success` or `skipped` exits 0. `unit: src/kernel/job_state_test.c::exit_zero_with_skips` (was an e2e case; see comments)
- [x] The job state machine only makes the transitions listed in the spec. Every illegal transition asserts. `unit: src/kernel/job_state_test.c::legal_transitions`, `::illegal_transition_asserts`

## Comments

Done. `bazel test //...` passes (39 tests).
- **State machine:** `src/kernel/job_state.{c,h}` has exactly the edges of design §7.2 (`pending → ready | skipped`, `ready → running`, `running → success | failed | terminating`, `terminating → cancelled`). An illegal transition prints both states and `abort()`s in every build type, not `assert`, which Bazel's `-c opt` compiles out. The unit test forks and checks for SIGABRT. Ticket 08 may need `ready → skipped` (a cancelled run with jobs waiting for a slot), and ticket 07 the `terminating` path; neither is wired up yet.
- **`exit_zero_with_skips` is a unit test, not an e2e case.** A job is only skipped after a dependency did not succeed, so no workflow can be all `success`/`skipped` with a skip in it until `if:` or the `always` join rule exist. The exit-code rule lives in `qwe_jobs_all_ok()` and is tested directly. `continue_on_error` covers the e2e side (a failed step, exit 0).
- **Order:** jobs run one at a time, and among the runnable ones in id order. The decoded workflow is a Lua table, which does not keep YAML key order; ticket 08's parallel runs are unordered anyway.
- **result.json:** a step that never ran (an earlier step failed) is `skipped` with a null reason and null times, and `changed: false`; a skipped job has null times and `steps: []`. A job's reason is its failing step's reason (`exit-code`); a `continue-on-error` failure leaves the job `success` with a null reason. Skipped jobs get no log file.
- **Refused for now:** `qwe run` exits 2 before running anything if a workflow uses `env`, `timeout-minutes`, `become`, `on`, `secret-outputs` or `with:` (or `uses:` steps, or a target other than `local`). The schema accepts them; silently ignoring them would run something other than what was written. Tickets 07, 11, 12, 14 and 10 lift these.
