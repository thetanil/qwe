# 08: A qualification report for every release

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 07

## What

The certificate covers a version (ADR-0015, "Consequences"). Each release needs
evidence that *that* binary was validated: which binary, built with which tools,
against which requirements, with which results, and which known malfunctions
apply.

## Fix

`release.yml` produces `qualification-report.md` (and a machine-readable
twin) and attaches it to the GitHub release. It contains:

- the qwe version, git SHA, and SHA-256 of each released binary;
- the build and analysis tool versions (GCC, Bazel, clang-tidy, luacheck;
  `.scratch/sca-round3` ticket 04 pins and records them);
- the requirement-to-test table from ticket 07, with each test's result in this
  release's run (the full suite, plus the asan, ubsan, valgrind and fuzz runs);
- the known-malfunction list from the safety manual, as of this release;
- the static-analysis evidence bundle from `.scratch/sca-round3` ticket 04, as
  supporting evidence.

A release whose report has a failed or missing requirement does not publish.

## Acceptance criteria

- [ ] A release run attaches the report. `human: needs a push and a release, which
      agents never do; the agent stops with the workflow ready and sets
      ready-for-human`
- [ ] The report generator runs locally on a built tree and produces every
      section. `unit: tools/ci/qualification_report_test.sh`
- [ ] A requirement whose test failed makes the generator exit non-zero.
      `unit: same`
- [ ] `tools/ci/workflows_test.sh` passes. `unit: workflows_test.sh`

## Comments
