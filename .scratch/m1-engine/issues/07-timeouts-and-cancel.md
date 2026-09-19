# 07: Timeouts and cancellation

Status: ready-for-agent
Type: task
Blocked by: 06

## What

Implement one teardown path for everything: SIGTERM to the step's process group, arm a timerfd grace timer (10 seconds), then SIGKILL. Three triggers enter it: step timeout, job timeout, and operator cancel (SIGINT or SIGTERM to qwe). Each trigger produces a different outcome and reason, as described in the spec.

## Acceptance criteria

- [ ] When a step times out, the step is `failed` with reason `timeout`. With `continue-on-error`, the job continues. `e2e: tests/e2e/step_timeout_fails/`, `tests/e2e/step_timeout_continue/`
- [ ] When a job times out during step 2 of 3, the job is `cancelled` with reason `timeout`, and step 3 never starts. `e2e: tests/e2e/job_timeout_cancels/`
- [ ] A step that traps SIGTERM and ignores it is SIGKILLed after the grace period. `e2e: tests/e2e/grace_then_sigkill/` (the test uses a shortened grace period via a test-only env override)
- [ ] `sh -c 'sleep 1000 & sleep 1000'` leaves no process alive after a timeout, because the whole group is killed. `unit: src/kernel/proc_test.c::group_kill_no_orphans`
- [ ] SIGINT to qwe: running jobs are `cancelled` with reason `cancel-requested`, pending jobs are `skipped`, qwe exits 130, and no children are left behind. `e2e: tests/e2e/operator_cancel/`
- [ ] The job timeout and the grace timer are separate timerfds, and firing one never re-arms the other. `unit: src/kernel/timer_test.c::timeout_and_grace_independent`
