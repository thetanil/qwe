# 12: Smoke: job graph, project plugin, and validation negatives

Status: ready-for-human
Category: enhancement
Type: task
Blocked by: 05, 08

## What

- `smoke_graph.yml`:
  - a diamond `a → {b, c} → d`, with `max-parallel: 2`;
  - b and c rendezvous: each writes its own file and waits (up to 10 s) for the other's, which proves
    they ran in parallel;
  - an output in `a` is read by a later step of `a`;
  - `d` asserts it has what it needs through files, since outputs are job-scoped.
  smoke.yml also runs it with `--job d` (the full needs closure runs) and `--job b` (only a and b; the
  summary has no c or d).
- `smoke_project_plugin.yml`, with `tests/smoke/.qwe/plugins/smoke.touch/` (a schema + a check/apply
  plugin that creates a file). It runs, then runs again in a second job (unchanged), and `file.read` +
  `assert` confirm the file.
- Negatives (each a `continue-on-error` GitHub step followed by an assertion step):
  - `neg_timeout.yml`: `timeout-seconds: 1` on `sleep 30`. Fails, the summary has reason `timeout`, and
    the GitHub step takes < 10 s (the assertion reads the job's timing).
  - `neg_dependency_failed.yml`: a failed job and two dependants. Exit 1, and the summary shows both
    dependants `skipped` / `dependency-failed`.
  - `neg_plugin_top_level/` (its own directory with a `.qwe/plugins/` plugin that has a top-level call):
    `qwe validate` exits non-zero with `plugin.lua:<line>:<col>`.
  - `neg_malformed.yml` (an unknown key in a step): `qwe validate` exits non-zero, naming the key and its
    path.

## Acceptance criteria

- [x] `smoke_graph.yml` and `smoke_project_plugin.yml` pass in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [x] All four negatives fail with their expected messages in the Bazel suite too (the smoke test runs `neg_*` expecting failure and greps the message kept in a comment line at the top of each file): `e2e: tests/smoke:smoke_workflows_test`
- [ ] On the runner: all green, the negatives orange and asserted, `--job` summaries show the right jobs: `manual: push; check the run and its summary` — not done; needs a real push (see 09/10/11's comments on this project's no-push rule)
- [x] `bazel test //...` green

## Comments

`smoke_graph.yml`: the diamond `a -> {b, c} -> d`, `max-parallel: 2`, b/c rendezvous
(poll up to 10s via `timeout-seconds: 10` and a 200x50ms loop, same pattern as
`tests/e2e/parallel_rendezvous`), a's later step reading an earlier step's own output, and
d checking `[ -e b.mark ] && [ -e c.mark ]` since job outputs don't cross jobs. Verified
`--job d` (full closure: a, b, c, d all run) and `--job b` (only a and b) for real
locally — both match the ticket's description exactly.

`smoke_project_plugin.yml` + `.qwe/plugins/smoke.touch/`: a minimal idempotent
check/apply plugin (stat, then touch) that goes through `ctx.backend` like the built-ins
do, not a bare `print()` like the existing `project_plugin_loads` e2e fixture — closer to
what a real project plugin looks like. First job creates the file (changed), second job
(needs: [first]) touches it again (unchanged) and confirms it with `file.read` + `assert`.

**The four negatives, and the new Bazel-suite mechanism**: `smoke_workflows_test.sh`
previously skipped every `neg_*.yml` unconditionally (those needed real permissions/
privilege only the GitHub job has). This ticket's four negatives need neither, so I added
a `# smoke: expect-fail: <message>` marker convention (new; the older neg_* files are
untouched, on purpose — retrofitting them felt like scope creep onto already-closed
tickets) that `smoke_workflows_test.sh` now acts on: run `qwe validate`; if that fails,
`<message>` must be in its output; if it passes, `qwe run --summary` must fail and
`<message>` must be in its output *or* its summary (`reason: timeout` and
`dependency-failed` only ever show up in the summary table, never on stdout — confirmed
by hand against `tests/e2e/summary_timeout` and `summary_failure_skip` before writing the
check). `neg_plugin_top_level/` is a directory (its own `w.yaml` + `.qwe/plugins/bad/`,
mirroring `tests/e2e/plugin_top_level_refused`'s fixture shape) rather than a flat file,
since a project-plugin negative needs its own `.qwe/plugins/`; the script has a second
loop for `neg_*/` directories using the same marker-in-`w.yaml` convention and the same
`expect_fail` helper.

`smoke_workflows_test.sh` also now copies `<smoke-dir>/.qwe` alongside every workflow it
runs (needed for `smoke_project_plugin.yml`'s `.qwe/plugins/smoke.touch/`), and
`tests/smoke/BUILD`'s `data` grows two more globs for the new `.qwe/plugins/**` and
`neg_plugin_top_level/**` fixture trees.

`smoke.yml` gets `--job d` / `--job b` steps for `smoke_graph.yml` in both `debug-smoke`
and `smoke` (the base "does it run" case is already covered by the generic
`tests/smoke:smoke_workflows_test` step inside `bazel test --config=release //...`, same
as `smoke_file.yml` — neither of those two ever got its own named debug-smoke/smoke step,
so I kept that precedent for `smoke_graph.yml` and `smoke_project_plugin.yml`'s base
runs, and only added dedicated steps for what the generic run can't exercise: `--job`
selection).

`bazel test //...` and `bazel run //tools/coverage:check` both green (245 tests pass, 3
sanitizer/valgrind smoke tests skipped as usual locally; no new plugin code, so the
coverage floor is untouched by this ticket).
