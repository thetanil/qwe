# 12: Smoke: job graph, project plugin, and validation negatives

Status: ready-for-agent
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

- [ ] `smoke_graph.yml` and `smoke_project_plugin.yml` pass in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [ ] All four negatives fail with their expected messages in the Bazel suite too (the smoke test runs `neg_*` expecting failure and greps the message kept in a comment line at the top of each file): `e2e: tests/smoke:smoke_workflows_test`
- [ ] On the runner: all green, the negatives orange and asserted, `--job` summaries show the right jobs: `manual: push; check the run and its summary`
- [ ] `bazel test //...` green

## Comments
