# 07: An OOM-injection harness

Status: ready-for-agent
Category: chore
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

- [ ] A test-only allocator shim fails the *n*th allocation and is otherwise transparent, selected by a build config so no shipping binary contains it. `unit: src/kernel/alloc_test.c::shim_fails_the_nth_call`
- [ ] Injecting at every *n* over a `qwe validate` run ends every time in success or a clean non-zero exit with a message: never a fault, never a hang. `unit: src/cli/validate/oom_test.c::validate_survives_every_injection`
- [ ] A clean failure names the file being validated, so the operator can tell what was being read when memory ran out. `unit: src/cli/validate/oom_test.c::validate_survives_every_injection`
- [ ] The same over a two-job `qwe run`, including a child that fails an allocation reporting a step result rather than dying silently. `unit: src/kernel/oom_test.c::run_survives_every_injection`
- [ ] A run that *succeeds* despite an injected failure is checked for having actually done the work, so a silently degraded success fails the test. `unit: src/kernel/oom_test.c::run_survives_every_injection`
- [ ] The allocation count for a fixed scenario is stable across runs, and the harness asserts that rather than assuming it. `unit: src/kernel/oom_test.c::injection_is_deterministic`
- [ ] `m1-review/07`'s bug is reproduced by the harness against the pre-fix code, as evidence that the outcome assertion is doing work. `manual: run the harness against the commit before m1-review/07 is fixed`

## Comments

Blocked by ticket 02: injecting into unchecked allocations only ever produces
the same NULL deref, which proves nothing and is tedious to sift.
