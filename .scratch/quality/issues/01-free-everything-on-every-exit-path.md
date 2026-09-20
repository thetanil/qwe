# 01: Free everything, on every exit path

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: none

## What

qwe is a one-shot CLI, so nothing it forgets to free is a leak that matters at
runtime: the process exits and the kernel takes it all back. That stops being
true the moment a leak checker is pointed at it. Ticket 04 puts valgrind in the
test gate, and valgrind cannot distinguish "deliberately left for exit" from "we
lost this pointer". Every one of these has to go before the gate is worth
having, or the gate reports hundreds of lines nobody reads.

What is left behind on a normal, successful run:

| What | Where |
|---|---|
| `jobs[]` itself | `qwe_run_workflow`, never freed |
| `job->id`, `job->target` | `strdup`ed in `qwe_jobs_load` |
| `job->needs[k]` and `job->needs` | `strdup`/`calloc` in `qwe_jobs_load` |
| `job->steps` | `calloc`ed in `job_start` |
| `step->id` | `strdup`ed in `step_id`, one per step |
| `step->outputs_json` | `strdup`ed in `store_outputs` |
| the job's Lua registry ref | `luaL_ref` in `qwe_jobs_load`, never unref'd |

`select_jobs` already frees `id`, `target` and `needs` for the jobs it drops,
so the shape of the cleanup exists; it just has no counterpart at the end of
the run.

Separately, several early returns in `qwe_run_workflow` skip everything,
including `lua_close`:

- `qwe_jobs_load` returning `< 0`
- `select_jobs` returning `< 0`
- `mkdir_p` failing
- `qwe_trace_open` failing

Each of those returns `QWE_EXIT_USAGE` directly, leaving `L` open and `jobs`,
`dir`, `run_dir` and `trace_path` allocated.

**How to do it.** A single `qwe_jobs_free(struct job *jobs, size_t n)` next to
`qwe_jobs_load`, owning everything `qwe_jobs_load` and the run put into a job,
and one cleanup label in `qwe_run_workflow` that every failure path jumps to.
The ordering matters in one place: the Lua refs have to be released before
`lua_close`, or they are freed twice in effect — harmless, but it muddies what
the checker reports.

Worth pinning while doing this: `qwe_sink_open` stores the `job` pointer it is
given rather than copying it, so the sink's lifetime is tied to the job's `id`.
That is true today and will stay true, but a freeing pass is exactly when it
gets broken, so it deserves a comment on the struct field.

## Acceptance criteria

- [ ] A successful run of a multi-job workflow with steps, outputs and `needs:` reports no reachable or unreachable blocks at exit under valgrind. `manual: valgrind --leak-check=full --errors-for-leak-kinds=all bazel-bin/src/cli/qwe run tests/e2e/needs_skip_chain/w.yaml` (the gate itself is ticket 04)
- [ ] The same holds for a run that fails, one that is cancelled with SIGINT, and one that hits a step timeout. `manual: as above, for tests/e2e/step_failure_stops_job, operator_cancel and step_timeout_fails`
- [ ] Every early return in `qwe_run_workflow` goes through one cleanup path: no `return` between the first allocation and the end of the function frees nothing. `unit: src/kernel/jobs_test.c::jobs_free_releases_everything`
- [ ] `qwe validate` on a valid and on an invalid workflow leaks nothing. `manual: valgrind on both`
- [ ] `qwe_jobs_free` is called with a partially built job list (a load that failed halfway) without faulting. `unit: src/kernel/jobs_test.c::jobs_free_handles_partial_load`
- [ ] The borrowed-pointer relationship between `qwe_sink` and the job's `id` is written on the struct field.

## Comments

This is a prerequisite for ticket 04 and makes ticket 03's output readable too;
a sanitizer's leak detector has the same problem with deliberate exit leaks.
