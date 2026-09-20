# 07: An OOM-injection harness

Status: resolved
Category: enhancement
Type: task
Blocked by: 02

## What

Once ticket 02 has put a check on every allocation, the checks themselves are
untested code — and they are the code that runs when things have already gone
wrong, which is the worst place for a wrong branch to hide. A few of them can
be provoked by hand; most cannot, because there is no way to make the fiftieth
`malloc` in a run fail on purpose.

The standard answer is a failing allocator: a test build where allocation is
routed through a shim that fails the *n*th call, and a harness that runs the
same scenario with *n* = 1, 2, 3 … up to however many allocations that scenario
makes. Every run must end in one of two ways — it succeeds, or it fails
cleanly with a message and a non-zero exit — and never in a fault, a hang, or a
success that is quietly missing something.

`m1-review/07` is the example of the third case, and the reason it is worth
building this: an allocation failure there made `qwe_dag_check` return "no
cycle", which is not a crash and not an error, just a wrong answer. A harness
that only checks for crashes would have missed it. So the assertion has to be
about the *outcome*, not just about survival.

**Scope it.** Exhaustive injection over a whole `qwe run` is a lot of
iterations and many of them exercise the same paths. Two scenarios are enough
to start:

- `qwe validate` on a real workflow with an inventory and a project plugin.
  Purely the load path, no forking, fully deterministic, and it covers the
  transcoder, the position table, the CBOR conversion, the Lua state and the
  validator.
- `qwe run` on a small two-job workflow. Adds job loading, the run directory,
  the ring, the sink and the event loop. Forking makes it slower and less
  deterministic, so run it over a smaller range of *n*.

The child after `fork` is its own problem: the shim's counter is inherited, and
a child that fails an allocation must report a step result rather than dying
silently — that behaviour is ticket 02's third rule, and this harness is what
checks it.

**Determinism is the thing to watch.** If the same *n* fails a different
allocation on two runs, the harness is useless for regression. Address-space
layout, hash ordering and anything time-dependent can all shift the count.
Check it by running the same *n* twice and comparing, before trusting any
result.

## Acceptance criteria

- [x] A test-only allocator shim fails the *n*th allocation and is otherwise transparent, selected by a build config so no shipping binary contains it. `unit: src/kernel/alloc_test.c::shim_fails_the_nth_call`
- [x] Injecting at every *n* over a `qwe validate` run ends every time in success or a clean non-zero exit with a message: never a fault, never a hang. `unit: src/cli/validate/oom_test.c::validate_survives_every_injection`
- [x] A clean failure names the file being validated, so the operator can tell what was being read when memory ran out. `unit: src/cli/validate/oom_test.c::validate_survives_every_injection`
- [x] The same over a two-job `qwe run`, including a child that fails an allocation reporting a step result rather than dying silently. `unit: src/kernel/oom_test.c::run_survives_every_injection`
- [x] A run that *succeeds* despite an injected failure is checked for having actually done the work, so a silently degraded success fails the test. `unit: src/kernel/oom_test.c::run_survives_every_injection`
- [x] The allocation count for a fixed scenario is stable across runs, and the harness asserts that rather than assuming it. `unit: src/kernel/oom_test.c::injection_is_deterministic`
- [x] `m1-review/07`'s bug is reproduced by the harness against the pre-fix code, as evidence that the outcome assertion is doing work. `manual: run the harness against the commit before m1-review/07 is fixed`

## Comments

Blocked by ticket 02: injecting into unchecked allocations only ever produces
the same NULL deref, which proves nothing and is tedious to sift.

### Resolved

- Shim: `src/kernel/oom_shim.{h,c}`, a `testonly` library, so no shipping target can depend on it; its `--wrap` linkopts travel with it (tests are `linkstatic`). Two counters, because a fork copies the counter: the arming process counts its own allocations (`qwe_oom_arm`), and each forked process counts from zero (`qwe_oom_arm_child`), logging to a file when it fires since its memory is gone. `qwe_oom_probe` runs one injected run in a process of its own (30 s alarm), so a fault, an abort or a hang is an outcome rather than the end of the test.
- Step children: the shim's "child" counter counts before `exec` only, which is the forked side of `child_argv` and of `qwe.exec` (the backend's own fork).
- `validate_survives_every_injection`: every n over a valid workflow (inventory, project plugin, `needs:`) and a cyclic one. Success must be silent; an invalid workflow must never come back valid; a failure must name a file under the workflow's directory (the inventory or plugin file may be what was being read) and say memory.
- `run_survives_every_injection`: every engine-side n over a two-job run (158 allocations), then each step child's 1st, 2nd... allocation. Exit 0 needs all work done and no failed/skipped in `result.json`; otherwise exit 1/2 with a message, or SIGABRT from `qwe_x*` naming the allocation (policy rule 2). A child failure must give a step reason of `engine-error` (or `plugin-error` from the backend's fork), never a bare `exit-code`.
- `injection_is_deterministic`: the count is equal over four runs, and the same n (and the same child m) ends the same way twice.
- Found by the harness, both fixed:
  - `plugins.lua`: `fs.list` failing (an allocation, or an unreadable `.qwe/plugins`) was read as "no project plugins", so the workflow came back invalid with `unknown plugin "hello"`. It is now a problem naming the directory.
  - `workflow.c`: a step child that could not encode its `exec` message printed to the log and exec'd anyway, so the parent saw a step that never started and reported success; likewise a check/apply result that could not be sent exited 0. `send_result` now reports failure; the child sends `failed`/`engine-error` and does not proceed.
- Manual criterion: with the `QWE_DAG_NO_MEMORY` hunk of `544b483` (m1-review/07) reverted in `dag.c`, `validate_survives_every_injection` fails at "allocation 186: an invalid workflow was reported valid". `dag.c` restored afterwards.
- `bazel test //...` green (188 pass, 3 skipped); the three new/changed tests also pass under `--config=asan`.
