# 02: smoke.yml builds once, runs once, compares with the stored values

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

Rework `.github/workflows/smoke.yml`:

- **build**: `bazel build --config=release //src/cli:qwe`, uploaded as
  `qwe-build`. No `bazel test --config=release //...`: `tests.yml` runs the same
  suite (the release config adds only `-g`), and `release.yml` and `nightly.yml`
  call `tests.yml` too. The gdb-backtrace step goes with it.
- **smoke**: the smoke workflows and the negative cases, once, against the
  release binary, with `--debug`. On failure, `.qwe/runs` (the lifecycle traces)
  is uploaded. `debug-smoke` is removed.
- **perf**: `run.sh` on the candidate only, then `compare.sh` against
  `tools/perf/expected.tsv`. A `perf-gate` input decides whether a regression
  fails the job: `true` from `release.yml` and by default on a manual dispatch,
  `false` on push and from `nightly.yml`. The report goes to the run summary
  either way. The `baseline` and `exclude-tag` inputs, and the release download,
  go.

Callers, docs and the consistency test change with it: `release.yml` passes
`perf-gate: true` and no `exclude-tag`; `docs/ci-checks.md`, the README's CI and
release sections, and `tools/perf/allow-list.txt`'s header describe the new flow.

## Acceptance criteria

- [ ] `smoke.yml` builds qwe once, runs no test suite, has no `debug-smoke` job,
      and runs the perf comparison against `tools/perf/expected.tsv`.
      `manual: read the workflow`
- [ ] `release.yml` gates on perf; push and nightly only report.
      `manual: read the workflows`
- [ ] `tools/ci/workflows_test.sh` passes. `unit: tools/ci/workflows_test.sh`
- [ ] The docs describe the new flow. `manual: docs/ci-checks.md, README.md`
- [ ] A push to `main` runs the new workflow green.
      `human: needs a push, which agents never do in this project`
- [ ] `bazel test //...` is green.

## Comments
