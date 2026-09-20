# 07: `qwe_dag_check` reports "no cycle" when its allocation fails

Status: ready-for-agent
Category: bug
Type: task
Blocked by: none

## What

`qwe_dag_check` allocates two arrays for the depth-first search and then runs
the search only if both succeeded:

```c
w.color = calloc(n ? n : 1, sizeof *w.color);
w.stack = calloc(n ? n : 1, sizeof *w.stack);
if (w.color && w.stack)
        for (i = 0; i < n && !found; i++)
                if (w.color[i] == WHITE)
                        found = visit(&w, i);
free(w.color);
free(w.stack);
return found ? err->status : QWE_DAG_OK;
```

If either `calloc` fails, the loop is skipped, `found` stays 0, and the
function returns `QWE_DAG_OK` — "this graph is a DAG". A failure to check is
reported as a successful check.

The consequence is not a crash, it is worse than one. Validation passes, the
run starts, and `run_all` reaches a state where every job is pending on a cycle
and none can be scheduled. The loop notices only through its backstop:

```c
if (running == 0) {
        rc = -1; /* cannot happen: validation rejects cycles */
        break;
}
```

The comment is the bug. It says the situation is impossible because validation
rejects cycles, which is exactly the guarantee that has just been skipped. The
run then fails with no message that points at the cycle.

This needs an allocation failure to reach, so it will not show up in normal
use. It matters here for two reasons. The first is that "the checker silently
approves when it cannot run" is a failure mode a certification reviewer looks
for by name, and it is one line to fix. The second is that this one is
different in kind from the general unchecked-allocation sweep
(`quality/02`): those crash, which is bad but honest, whereas this one
returns a wrong answer and keeps going.

**The fix.** Return a distinct status when the check could not be performed,
and have the caller treat it as a validation failure with a clear message.
While there, the `running == 0` backstop in `run_all` should say what actually
happened rather than claiming it cannot happen — if it is ever reached, it is
the only evidence the operator will get.

## Acceptance criteria

- [ ] A failed allocation in `qwe_dag_check` returns a status distinct from `QWE_DAG_OK`, and the caller turns it into a validation error that names the workflow. `unit: src/kernel/dag_test.c::allocation_failure_is_not_ok` (with an injectable allocator, or by checking the status the function returns for a fabricated failure)
- [ ] A real cycle is still reported exactly as it is today, with the `a -> b -> a` path in the message. `unit: src/kernel/dag_test.c` (existing cases, must stay green)
- [ ] The `running == 0` branch in `run_all` prints why the run cannot continue instead of failing silently, and its comment no longer asserts the case is impossible. `manual: read src/kernel/workflow.c run_all; no automated case can reach the branch once the dag check is correct`
- [ ] A workflow with no jobs at all is still rejected by the schema before any of this is reached. `e2e: tests/e2e/validate_unknown_key/` (existing, must stay green)

## Comments

The rest of the unchecked-allocation work is `quality/02`. This one is split
out because it changes an answer rather than crashing, and because it is small
enough to fix on its own.
