# 04: A valgrind gate over the unit tests and a chosen set of e2e cases

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

Valgrind finds a different class of thing from ASan — uninitialised reads that
ASan does not model, and it needs no rebuild, so it can run against the
shipping binary rather than an instrumented one. That is worth having on a tool
being certified: it checks the artefact, not a variant of it.

Two obstacles decide the shape of this.

**Leaks must be fixed first.** That is ticket 01, and this ticket is blocked on
it. Until then `--leak-check=full` reports the per-run allocations that are
deliberately left for exit, and a report nobody can read is a gate nobody
keeps.

**qwe forks, and valgrind follows.** With `--trace-children=yes` every step
child is traced too, which is the interesting part — `child_argv` runs Lua in
the child — but children `execvp` into `sh`, `ssh` and whatever the workflow
runs, and tracing those is pure noise and very slow. `--trace-children-skip`
with the right patterns is the tool for that. Without it, an e2e suite under
valgrind takes long enough that nobody runs it.

So the gate is two pieces, not one:

- **Every `cc_test`, under valgrind, always.** These are fast, they do not
  fork, and they cover the pure logic: lifecycle, sched, ring, dag, redact,
  envelope, preamble, proc, timer, transcode, positions. This should be cheap
  enough to run on every change.
- **A named subset of e2e cases, under valgrind, on a slower cadence.** Pick
  for coverage of the paths unit tests cannot reach — one plain run, one with
  plugins, one with secrets and redaction, one cancellation, one timeout, one
  `needs:` chain. Not the ssh cases: they depend on a reachable host and
  already skip.

A suppression file will be needed for LuaJIT, which is known to make valgrind
complain about its own allocator and stack handling. Keep it small and comment
every entry with why it is there — an unexplained suppression is how a real
finding gets hidden.

## Acceptance criteria

- [ ] `bazel test --config=valgrind //...` runs every `cc_test` under `valgrind --leak-check=full --error-exitcode=1` and passes.
- [ ] A chosen set of e2e cases runs under valgrind through a separate target, and passes. `e2e: tests/e2e/needs_skip_chain/`, `tests/e2e/file_ensure_idempotent/`, `tests/e2e/secret_redacted_env/`, `tests/e2e/operator_cancel/`, `tests/e2e/step_timeout_fails/`, `tests/e2e/run_outputs/`
- [ ] Step children are traced; the programs they exec into are not. `manual: confirm from a valgrind log that child_argv's Lua is covered and that sh and ssh are skipped`
- [ ] The LuaJIT suppression file has an entry-by-entry comment saying what each one hides and why it is not a real finding.
- [ ] A deliberately introduced uninitialised read is caught by the gate. `unit: src/kernel/valgrind_smoke_test.c` (tagged to run only under this config)
- [ ] How long each piece takes is measured and written down, and the cadence for the e2e piece is chosen from that number rather than guessed.

## Comments
