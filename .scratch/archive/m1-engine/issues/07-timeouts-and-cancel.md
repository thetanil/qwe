# 07: Timeouts and cancellation

Status: resolved
Type: task
Blocked by: 06

## What

Implement one teardown path for everything: SIGTERM to the step's process group, arm a timerfd grace timer (10 seconds), then SIGKILL. Three triggers enter it: step timeout, job timeout, and operator cancel (SIGINT or SIGTERM to qwe). Each trigger produces a different outcome and reason, as described in the spec.

## Acceptance criteria

- [x] When a step times out, the step is `failed` with reason `timeout`. With `continue-on-error`, the job continues. `e2e: tests/e2e/step_timeout_fails/`, `tests/e2e/step_timeout_continue/`
- [x] When a job times out during step 2 of 3, the job is `cancelled` with reason `timeout`, and step 3 never starts. `e2e: tests/e2e/job_timeout_cancels/`
- [x] A step that traps SIGTERM and ignores it is SIGKILLed after the grace period. `e2e: tests/e2e/grace_then_sigkill/` (the test uses a shortened grace period via a test-only env override)
- [x] `sh -c 'sleep 1000 & sleep 1000'` leaves no process alive after a timeout, because the whole group is killed. `unit: src/kernel/proc_test.c::group_kill_no_orphans`
- [x] SIGINT to qwe: running jobs are `cancelled` with reason `cancel-requested`, pending jobs are `skipped`, qwe exits 130, and no children are left behind. `e2e: tests/e2e/operator_cancel/`
- [x] The job timeout and the grace timer are separate timerfds, and firing one never re-arms the other. `unit: src/kernel/timer_test.c::timeout_and_grace_independent`

## Comments

Done. `bazel test //...` passes (45 tests); the timing-sensitive e2e cases, `proc_test` and `timer_test` were also run 15–20 times in parallel.
- **One teardown path** (`run_step` in `src/kernel/workflow.c`): SIGTERM to the process group (`qwe_proc_kill_group`), a grace timerfd armed **once**, SIGKILL when it fires. When the leader exits during teardown the group is SIGKILLed once more so nothing it left behind survives. Triggers are a step timeout, the job's timer and an operator cancel (a `signalfd` on SIGINT/SIGTERM); if several fire, the strongest decides the outcome (cancel > job timeout > step timeout) without restarting the teardown.
- **Outcomes:** a step timeout is `failed`/`timeout` and `continue-on-error` applies. A job timeout or operator cancel makes the step and job `cancelled` (`timeout` / `cancel-requested`), `continue-on-error` cannot rescue it, and later steps are `skipped`. A job that fails from a step timeout gets the reason `timeout` too (its failing step's reason). Jobs run `running → terminating → cancelled` through the state machine; jobs still pending on an operator cancel are `skipped` with `cancel-requested`, and qwe exits 130 even if another job had failed. Triggers are also checked between steps and between jobs, so a timeout or SIGINT that lands while no step is running is not missed.
- **A step that exits as its trigger fires is not reclassified:** the trigger only counts if teardown began before the step exited.
- **`src/kernel/timer.c`:** a one-shot timerfd wrapper with a sticky `fired`. The unit test shows the timeout and grace timers are separate fds and that firing, arming or disarming one leaves the other's deadline untouched.
- **`timeout-minutes` accepts fractions** (the tests use 0.005–0.02). That exposed a transcoder gap: YAML floats like `0.01` were being read as strings. The transcoder now resolves core-schema floats (`transcode_test.c::floats_resolve`).
- **Test-only overrides:** `QWE_TEST_GRACE_MS` shortens the 10 s grace (the spec says fixed at 10 s otherwise; nothing but the e2e harness sets it).
- **Harness additions** (`tests/e2e/run_case.sh`): an `env` file (KEY=VALUE lines), a `signal` file (`<seconds> <SIGNAME>`: qwe runs in the background and gets that signal), and `QWE_E2E_MARK`, a number unique to each run, so a check can find its own leftover processes without tripping over the same test running in parallel. (The first version of `operator_cancel` used a fixed marker and failed 11 of 15 parallel runs; that is how the marker came about.)
- **Still refused by `qwe run`:** `env`, `become`, `on`, `secret-outputs`, `with:` and `uses:`. `timeout-minutes` is now supported at job and step level.
