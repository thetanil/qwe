# 08: Parallel jobs and `max-parallel`

Status: ready-for-agent
Type: task
Blocked by: 07

## What

Let jobs run at the same time: `ready → running` is limited only by `max-parallel` (default unlimited). One event loop, many live process groups, one ring and one log per job. This ticket uses the `local` backend only. The ssh session cap is ticket 13.

The tests below must give a definite pass or fail rather than depend on timing. The rendezvous test only passes if the jobs really run at the same time.

## Acceptance criteria

- [ ] Rendezvous: jobs A and B each create a marker file, then wait up to 5 seconds for the other's marker. Both succeed. `e2e: tests/e2e/parallel_rendezvous/`
- [ ] The same workflow with `max-parallel: 1`: the first job to start is `failed` with reason `timeout` (its step timeout is shorter than the wait). This proves the limit is enforced. `e2e: tests/e2e/parallel_limit_enforced/`
- [ ] Failure isolation: A fails while B is running. B finishes `success`, A's dependents are `skipped` with reason `dependency-failed`, and qwe exits 1. `e2e: tests/e2e/parallel_failure_isolation/`
- [ ] Cancel during a parallel run: SIGINT while 3 jobs are running. All 3 are `cancelled` with reason `cancel-requested` and no children are left. `e2e: tests/e2e/parallel_cancel/`
- [ ] Log separation: two jobs each print 50,000 distinct tagged lines at the same time. Each job's log contains exactly its own lines, in order. `e2e: tests/e2e/parallel_log_separation/`
- [ ] The scheduler never has more than `max-parallel` jobs `running`, checked across randomized graphs. `unit: src/kernel/sched_test.c::never_exceeds_max_parallel`
