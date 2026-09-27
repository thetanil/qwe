# 03: A workflow that measures and proposes new expected values

Status: ready-for-agent
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

- [ ] The workflow runs only by `workflow_dispatch`, and the consistency test
      says so. `unit: tools/ci/workflows_test.sh`
- [ ] It has a README badge. `unit: tools/ci/workflows_test.sh` (rule 1)
- [ ] The docs say when and how to run it. `manual: docs/ci-checks.md`
- [ ] A dispatch opens a PR with a valid `expected.tsv`.
      `human: needs the workflow on GitHub, and "Allow GitHub Actions to create
      and approve pull requests" enabled in the repo settings`
- [ ] `bazel test //...` is green.

## Comments
