# 08: Parallel jobs and `max-parallel`

Status: resolved
Type: task
Blocked by: 07

## What

Let jobs run at the same time: `ready → running` is limited only by `max-parallel` (default unlimited). One event loop, many live process groups, one ring and one log per job. This ticket uses the `local` backend only. The ssh session cap is ticket 13.

The tests below must give a definite pass or fail rather than depend on timing. The rendezvous test only passes if the jobs really run at the same time.

## Acceptance criteria

- [x] Rendezvous: jobs A and B each create a marker file, then wait up to 5 seconds for the other's marker. Both succeed. `e2e: tests/e2e/parallel_rendezvous/`
- [x] The same workflow with `max-parallel: 1`: the first job to start is `failed` with reason `timeout` (its step timeout is shorter than the wait). This proves the limit is enforced. `e2e: tests/e2e/parallel_limit_enforced/`
- [x] Failure isolation: A fails while B is running. B finishes `success`, A's dependents are `skipped` with reason `dependency-failed`, and qwe exits 1. `e2e: tests/e2e/parallel_failure_isolation/`
- [x] Cancel during a parallel run: SIGINT while 3 jobs are running. All 3 are `cancelled` with reason `cancel-requested` and no children are left. `e2e: tests/e2e/parallel_cancel/`
- [x] Log separation: two jobs each print 50,000 distinct tagged lines at the same time. Each job's log contains exactly its own lines, in order. `e2e: tests/e2e/parallel_log_separation/`
- [x] The scheduler never has more than `max-parallel` jobs `running`, checked across randomized graphs. `unit: src/kernel/sched_test.c::never_exceeds_max_parallel`

## Comments

Done. `bazel test //...` passes (51 tests), also run 5 times over.
- **One event loop, many jobs** (`src/kernel/workflow.c`): the blocking `run_job`/`run_step` pair is now a per-job state record (`struct job_run`) driven by a single epoll. Each fd carries a tag (`struct ev`) naming its job and kind (output, step timer, job timer, grace timer), plus one SIGCHLD signalfd and the cancel signalfd. On SIGCHLD every live step is `waitpid(WNOHANG)`-checked. Events can be stale within a batch (the step ended earlier in it), so every handler checks the live state and timers are checked by `qwe_timer_expired`, not by the event alone. Teardown, triggers and outcomes are unchanged from 07.
- **Scheduler** (`src/kernel/sched.c`): a pure function over states and the `needs:` graph. It resolves pending jobs to skipped/ready (cascading in one pass) and returns which ready jobs fit under `max-parallel` in id order. `never_exceeds_max_parallel` runs 2000 random DAGs with random completions. `max-parallel` (workflow level, absent means unlimited) is read from the decoded workflow.
- **Operator cancel** now tears down every running job in the same loop iteration and skips all pending ones; nothing new starts.
- **Existing e2e cases changed:** `operator_cancel` and `needs_skip_chain` have independent jobs whose old golden output relied on jobs running one at a time. They now say `max-parallel: 1`, which keeps their meaning (and their deterministic order).
- A job whose log cannot be opened now ends `failed` with reason `internal` instead of aborting the whole run.
- `sink.c` opens logs `O_CLOEXEC` so a step's children do not inherit other jobs' log fds.
- The rendezvous and log-separation cases wait on each other's marker files, so they only pass if the jobs really overlap.
