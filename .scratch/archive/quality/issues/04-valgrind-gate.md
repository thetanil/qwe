# 04: A valgrind gate over the unit tests and a chosen set of e2e cases

Status: resolved
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

- [x] `bazel test --config=valgrind //...` runs every `cc_test` under `valgrind --leak-check=full --error-exitcode=1` and passes.
- [x] A chosen set of e2e cases runs under valgrind through a separate target, and passes. `e2e: tests/e2e/needs_skip_chain/`, `tests/e2e/file_ensure_idempotent/`, `tests/e2e/secret_redacted_env/`, `tests/e2e/operator_cancel/`, `tests/e2e/step_timeout_fails/`, `tests/e2e/run_outputs/`
- [x] Step children are traced; the programs they exec into are not. `manual: confirm from a valgrind log that child_argv's Lua is covered and that sh and ssh are skipped`
- [x] The LuaJIT suppression file has an entry-by-entry comment saying what each one hides and why it is not a real finding.
- [x] A deliberately introduced uninitialised read is caught by the gate. `unit: src/kernel/valgrind_smoke_test.c` (tagged to run only under this config)
- [x] How long each piece takes is measured and written down, and the cadence for the e2e piece is chosen from that number rather than guessed.

## Comments

### Resolved

Write-up (commands, tracing rules, suppressions, timing, cadence) is `docs/valgrind.md`. `bazel test //...` and `bazel test --config=valgrind //...` pass; `bazel test //tests/e2e:valgrind_e2e` passes.

- `--config=valgrind` sets `--run_under=//tools/valgrind:run_under`, which wraps compiled tests in `valgrind --leak-check=full --error-exitcode=1` and passes shell tests through untouched (valgrind on a shell would trace the shell).
- Six e2e cases run as `<case>_valgrind_test` (tag `manual`, suite `//tests/e2e:valgrind_e2e`). `valgrind_case.sh` fails on any non-empty valgrind log, not just the exit code, because a signalled or exec'd step child never returns `--error-exitcode`.
- **Children:** confirmed by hand on `run_outputs`: three traced processes (qwe and its two step children, whose Lua runs under valgrind before the exec), no `sh`. Skip patterns are `/bin/*,/usr/*,/sbin/*,/lib/*`.
- **Suppressions: none needed.** The suite is clean under valgrind 3.22 with LuaJIT unsuppressed, so `luajit.supp` has only its rule at the top. The "entry-by-entry" criterion is met vacuously; a future entry must carry a comment.
- **Liveness:** `valgrind_smoke_test` (only a test under this config) passes only if valgrind fails a child that branches on uninitialised memory. Verified it fails when run without valgrind.
- **Timing:** plain tests ~3 s; the valgrind cc_test gate ~130 s, of which `load_oom_test` is 128.8 s (0.5 s plain; it starts a LuaJIT VM per injected failure); without it ~6 s; the six e2e cases ~6 s together. From those numbers, both pieces are cheap enough per change; if 130 s is too long, run `load_oom_test` nightly only.
- No findings in our code: the gate was green on first run.
