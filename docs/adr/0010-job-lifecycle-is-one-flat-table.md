# The job lifecycle is one flat, table-driven state machine

Every change to a job's state, from the moment the workflow is parsed to its outcome, is decided by one pure function. It looks up (state, event) in a flat table and returns the next state, a reason and a list of actions (spawn, SIGTERM the group, arm grace, SIGKILL the group, record). The phase of the running step is part of the job's state. There is no separate step state machine, no guard bits and no flags beside the state. The epoll loop only turns fds into events and carries out the actions it gets back. The scheduler still makes the decisions that span jobs (needs, parallelism slots), but it changes no job's state: it sends each job `needs-met`, `needs-failed` or `slot-granted`.

We chose this so that the lifecycle can be certified. Tickets 06–08 kept outcomes in loose flags spread across the event loop, and a review found cancel, teardown and transition bugs that slipped between them (ticket 17). A single table can be read by an assessor, and it can be tested exhaustively without the test restating it.

## States

```
pending  ready  between-steps
step-running        step-running-coe
step-stopping       step-stopping-coe      (step timeout: TERM sent, grace armed)
step-killing        step-killing-coe       (grace expired: KILL sent)
settling-{continue,fail,cancel-timeout,cancel-requested}-{term,kill}
success  failed  skipped  cancelled
```

- **`settling-*`:** the outcome is decided, and the lifecycle waits for the step's process group to be empty. The first part says what happens next, and the second says which teardown signal has been sent. It covers stragglers after a normal exit and a job-level teardown. It replaces design §7's `terminating`.
- **`coe` in the state:** continue-on-error is the only step attribute that changes control flow, so it is the only one that doubles the step states. A new such attribute would double them again, and the table makes that cost visible.
- **`between-steps`:** "are there more steps?" is not a guard. The shell holds the step cursor and sends `next-step`, `no-more-steps` or `start-failed`.

## Events

`needs-met`, `needs-failed`, `slot-granted`, `next-step`, `no-more-steps`, `start-failed`, `leader-exit-ok`, `leader-exit-fail`, `step-timeout`, `job-timeout`, `cancel`, `grace-expired`, `group-empty`, and later `skip` (a job that has not started is not to run at all: its reason comes from the payload, and only `pending` and `ready` accept it; added for disabled targets, `m1-review/10`). That is 21 states × 14 events, roughly 290 cells. Two things travel as payload rather than as extra events: `leader-exit-fail`'s reason, and `next-step`'s continue-on-error flag, which picks between the plain and `coe` step-running states. A cancel that finds the job in `between-steps` (no step live, so nothing to settle) goes straight to `cancelled`. A spawn that fails is `start-failed` in `step-running`/`step-running-coe`, because the state is entered before the spawn action runs; `start-failed` in `between-steps` is a job whose log cannot be opened.

## Rules

- **Every cell is exactly one of three kinds.**
  - **Transition:** a new state, a reason and actions.
  - **Ignore:** a stale, harmless event, with a written justification.
  - **Impossible:** a kernel bug. qwe fails and stops (abort), after flushing a trace line.
- **Stale events are filtered by step index** before the table sees them. Each step-scoped event carries the index of its step, so an event from step 1 never reaches step 2.
- **Failure reasons are payload.** `leader-exit-fail` carries the step's own reason (`exit-code`, `not-converged`, `plugin-error`, …). The table records it and never branches on it, so a new reason adds no column.
- **A step is over when its process group is empty,** not when its leader exits. Survivors of the leader get the normal teardown with the full grace period. qwe is a child subreaper so that it can see the group empty. A process that leaves the group (`setsid`) is a known M1 gap: see design §9.3 and `.scratch/step-containment/issues/01-cgroup-per-step.md`.
- **Every transition, ignore and abort goes to the lifecycle trace** (`lifecycle.trace` in the run directory). `--debug` adds every event.

## Verification

Four layers, none of which restate the table:

1. **Completeness:** every cell is classified, and no cell is missing.
2. **Rules over all cells:** each rule is written once and checked against every cell. For example:
   - A final state never changes.
   - SIGKILL is only sent after SIGTERM.
   - The grace timer is armed only once per step.
   - A cancel wins.
   - An ignore never changes the state.
3. **Exhaustive exploration:** a breadth-first search from `pending`, driven by an event model in its own file (which events can occur in which state). It shows that every cell that is not impossible is reachable, and that no event sequence that can really happen reaches an impossible cell.
4. **Scenario walks:** named event sequences checked against an expected trace, one for each behaviour in the spec.

The e2e tests only check that the shell carries out the actions the table asks for. They compare golden files of each job's trace lines.

## Considered options

- **Two machines, one for jobs and one for steps.** Rejected. Each is small, but correctness depends on the coupling between them (56 combined states, of which about 12 can occur), and that coupling would live in untested glue. That glue is where every bug in ticket 17 was.
- **Guard bits in the key (`coe`, `more`).** Rejected. Once expanded it is larger (about 900 cells), and a table whose key includes a bitset is hard to explain to an assessor.
- **Conditions inside a cell's code.** Rejected. The exhaustive layers cannot see inside an `if`, so the table stops being something to review.
- **Ignore and log impossible cells rather than aborting.** Rejected. It would carry a kernel bug forward silently. Fail-stop, together with a proof that impossible cells cannot be reached, is the stronger claim.

## Consequences

- Check/apply (ticket 10) adds no states. By ADR-0001 its phases run inside the step's single child process. It only adds reasons as payload.
- The ssh ControlMaster (ticket 13) is outside every step's process group, so it adds no states either. Re-establishing it belongs in `between-steps`, before `next-step`.
- `engine-error` (qwe could not start something) is the `start-failed` event, so the table test covers it without making a `fork` fail for real.
