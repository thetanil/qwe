# 17: Consolidate the job and step lifecycle (cancel, teardown, state transitions)

Status: needs-triage
Type: task
Blocked by: 08

## What

Tickets 06–08 grew the job and step lifecycle piece by piece, and a review of `HEAD~3...HEAD` found that it no longer holds together. Outcomes are decided in several places (`step_begin`, `step_record`, `job_finish` and the cancel loop in `run_all`, all in `src/kernel/workflow.c`). They are driven by loose flags (`failed`, `torn`, `tearing`, `step_trigger`, `step_live`) rather than by the state machine in `src/kernel/job_state.c`. Each corner case has been patched where it showed up, and the review found more that were missed. This needs a design decision before more patching. Should the job and step lifecycle become one explicit state machine, where each trigger (step exit, step timeout, job timeout, operator cancel, grace expiry) is an event with one handler that owns the transition and the reason?

This ticket is for triage. It may turn into a grilling or prototype ticket before any code changes.

## Findings from the review (2026-09-19)

Already fixed while this ticket was being filed (not committed yet):

- **The e2e harness never compared `expected/RUN/*` goldens under Bazel.** `run_case.sh` listed goldens with `find . -type f`, and Bazel stages them as symlinks, so it found nothing. Only exit, stdout, stderr and `check.sh` were ever checked. Now it uses `find -L`, and `harness_selftest_run_golden` guards against regression. The `result.json` goldens of tickets 06–08 had never actually been checked.
- **An operator cancel started jobs that were `ready` and waiting for a `max-parallel` slot.** They ended `cancelled`, with times and a log, instead of `skipped`. `operator_cancel` was failing and only passed because of the harness bug. The state machine now has `ready → skipped` (reason `cancel-requested`), and design §7.2 has been amended to match.

Still open:

1. **`terminating` is a state only on paper** (design §7.1: "Teardown in progress"). A job stays `running` through its teardown, and `job_finish` moves it `terminating → cancelled` at once. `sched.c` counts `terminating` as live, but no job is ever in that state long enough to be counted.
2. **The grace period is cut short** (spec §3: "SIGTERM …, a 10-second grace period, then SIGKILL"). `step_reap` SIGKILLs the whole group as soon as its leader exits, so a child that traps SIGTERM loses the rest of its grace period.
3. **Step outcomes and reasons are string literals** spread across branches. `t == TRIG_CANCEL ? "cancel-requested" : "timeout"` appears in both `step_record` and `job_finish`, and `failed` together with `torn` encodes the job outcome implicitly. Job states are an enum, but step states are not.
4. **The job-start failure path** (the log cannot be opened) invents a reason, `internal`, that spec §2 does not list, and leaks the job's ring and timer.
5. **The tests that should prove teardown are weak:**
   - `grace_then_sigkill` only proves that the shell died; an immediate SIGKILL would pass it too.
   - `operator_cancel` and `parallel_cancel` send their signal at a fixed 0.7 s, which ticket 08 asks to avoid. Starting the cancel when a marker file appears would remove the timing dependence.
6. **`workflow.c` does too much** (about 800 lines): loading jobs and refusing unimplemented keys, the event loop, teardown, scheduler glue and building the result. The lifecycle could move into its own module, and `load_jobs` into another.

## Acceptance criteria

To be written at triage. Candidates:

- [ ] Every job and step transition goes through one function per state machine, and a unit test walks every edge, including cancel from each state. `unit: src/kernel/job_state_test.c::…`
- [ ] A job stays `terminating` for the whole teardown. `unit:` against the scheduler, or `e2e:` via a slow-to-die step.
- [ ] A child that traps SIGTERM still gets the full grace period after its group leader exits. `e2e: tests/e2e/grace_outlives_leader/`
- [ ] `grace_then_sigkill` fails if SIGKILL comes before the grace period ends.
- [ ] The cancel e2e cases start their signal from a marker file, not a fixed delay.

## Comments
