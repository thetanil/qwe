# 02: smoke.yml builds once, runs once, compares with the stored values

Status: resolved
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

- [x] `smoke.yml` builds qwe once, runs no test suite, has no `debug-smoke` job,
      and runs the perf comparison against `tools/perf/expected.tsv`.
      `manual: read the workflow`
- [x] `release.yml` gates on perf; push and nightly only report.
      `manual: read the workflows`
- [x] `tools/ci/workflows_test.sh` passes. `unit: tools/ci/workflows_test.sh`
- [x] The docs describe the new flow. `manual: docs/ci-checks.md, README.md`
- [ ] A push to `main` runs the new workflow green.
      `human: needs a push, which agents never do in this project`
- [x] `bazel test //...` is green.

## Comments

2026-09-27: done, except the first real run: "a push to `main` runs the new workflow
green" is open until the user pushes (agents never push in this project).

- `smoke.yml` has three jobs: `build` (one `bazel build --config=release //src/cli:qwe`,
  uploaded), `smoke` (every smoke workflow and negative case once, with `--debug`, with the
  traces uploaded as `smoke-runs` on failure), and `perf` (candidate only, against
  `tools/perf/expected.tsv`). Gone: `debug-smoke` (its second, fastbuild binary and second
  run of every workflow), `bazel test --config=release //...` and its gdb-backtrace step,
  and the `baseline`/`exclude-tag` inputs.
- Gating: a new `perf-gate` input (boolean). `release.yml` passes `true`, a manual
  dispatch defaults to `true`, and a push or `nightly.yml` leaves it `false`. Exit 1 from
  `compare.sh` (a regression) then only fails the job when gating, and is otherwise a
  warning with the report in the run summary. Any other exit (a malformed expected file)
  always fails the job.
- `workflows_test`'s `command_unwired` case failed at first. My new comments in `smoke.yml`
  quoted the `bazel test //...` command verbatim, and rule 2 is a substring match, so the
  comment satisfied it (the caveat `docs/ci-checks.md` already warns about). The comments
  no longer quote the command.
- Docs: `docs/ci-checks.md` (the Smoke row, the "Every push" commands, the nightly and
  release paragraphs) and the README's CI table and release steps.
- `actionlint` is clean apart from a pre-existing shellcheck note in `release.yml`'s
  publish step. `bazel test //...`: 262 pass, 3 skipped.
