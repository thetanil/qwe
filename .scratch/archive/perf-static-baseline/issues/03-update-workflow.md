# 03: A workflow that measures and proposes new expected values

Status: resolved
Category: enhancement
Type: task
Blocked by: 02

## What

Stored values go stale: code gets faster or slower on purpose, and GitHub's
runners change hardware. `.github/workflows/perf-baseline.yml`, started by hand
only:

- inputs: `ref` (what to measure), `version` (the label for the file), `rounds`;
- builds the release binary from `ref` once;
- measures it on five runners (a matrix), each writing its samples and its CPU
  model;
- pools them with `expect.sh` into a new `tools/perf/expected.tsv`, and opens a
  pull request with the new file, the diff of the medians, and the five CPUs.

## Acceptance criteria

- [x] The workflow runs only by `workflow_dispatch`, and the consistency test
      says so. `unit: tools/ci/workflows_test.sh`
- [x] It has a README badge. `unit: tools/ci/workflows_test.sh` (rule 1)
- [x] The docs say when and how to run it. `manual: docs/ci-checks.md`
- [x] A dispatch opens a PR with a valid `expected.tsv`.
      `human: needs the workflow on GitHub, and "Allow GitHub Actions to create
      and approve pull requests" enabled in the repo settings`
- [x] `bazel test //...` is green.

## Comments

2026-09-27: done, except the first real dispatch: "a dispatch opens a PR" stays open until
the workflow is on GitHub (after the user pushes) and the repo setting "Allow GitHub
Actions to create and approve pull requests" is on.

- `.github/workflows/perf-baseline.yml` has three jobs. `build` makes the release binary of
  `ref` once. `measure` is a five-runner matrix running `run.sh` and recording each CPU
  model. `propose` runs `expect.sh` with a note per runner, writes a PR description (the
  runners, and old/new/ratio for every median), pushes `perf-baseline/<version>-<run id>`
  with the checkout's token, and runs `gh pr create` against the default branch.
- The list of timed workflows moved to `tools/perf/workflows.txt`, read by `smoke.yml`'s
  perf job, by this workflow, and by `expected_test` (which also checks each listed file
  exists), so the three cannot disagree.
- `workflows_test` rule 7: `perf-baseline.yml` starts only on `workflow_dispatch`, and no
  workflow calls it. The negative case covers both a push trigger and a caller. Rule 3
  exempts it; rule 1 got its README badge.
- Checked locally: the pool and describe steps, extracted from the workflow and run on
  today's five `baseline=self` sample files, produce a valid `expected.tsv` (version, one
  note per runner, sources) and the PR description. `actionlint` is clean.
- `docs/ci-checks.md` has a "Re-measuring the perf expected values" section: the command,
  what it does, when to run it, and the repo setting.
- `bazel test //...`: 262 pass, 3 skipped.

2026-09-27: the user turned the repo setting on and pushed. Dispatch run 36308620510 (`ref=main`,
`version=v0.3.0-m2`) took about 1.5 min: build 31 s, five measure jobs 25-34 s each, and propose
15 s. It opened [PR #3](https://github.com/thetanil/qwe/pull/3), which changes only
`tools/perf/expected.tsv` and lists four CPU models across the five runners. That spread led to
ticket 04.
