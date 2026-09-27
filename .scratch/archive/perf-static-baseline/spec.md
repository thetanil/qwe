# Perf: a stored baseline, and a smoke workflow that builds and runs once

Status: ready-for-agent

## What

The perf job (`release-smoke`, ticket 15; ADR-0014) times every smoke workflow
against the newest release's binary, downloaded and run side by side. That has
three problems:

- **It depends on old binaries running new workflows.** The v0.3.0 release
  gated one workflow out of four (run 36186458092): the v0.2.0 binary could not
  run the other three, which use features added later. Every new feature repeats
  this.
- **It measures twice to learn once.** Each round runs both binaries, so half of
  every perf job re-measures a binary that has not changed since its release.
- **The smoke workflow repeats itself.** It builds qwe twice (a debug build for
  `debug-smoke`, the release build), reruns the whole test suite as
  `bazel test --config=release //...` (which `tests.yml` already runs;
  `--config=release` only adds `-g`), and runs every smoke workflow twice.

Decided with the user on 2026-09-27:

- **Stored expectations.** `tools/perf/expected.tsv` holds per-key medians and
  p90s, measured once. The perf job runs only the candidate and compares it with
  them. The first values come from the v0.3.0 binary: 255 rounds per key over
  five runs on five runners (the v0.3.0 release run's candidate side, and the
  baseline side of the four `main` pushes that compared against v0.3.0).
- **Gate on release, report elsewhere.** A release (and a manual dispatch) fails
  on a regression. Push and nightly run the same comparison but only report, so
  runner drift never turns `main` red.
- **Build once, run once.** One release build, uploaded as an artifact. The
  smoke workflows run once against it with `--debug`, replacing `debug-smoke`.
  No second copy of the test suite.
- **An update workflow opens a PR.** It measures a chosen ref on five runners,
  pools the samples into a new `expected.tsv`, and opens a pull request with the
  diff and the runners' CPUs.

This supersedes ADR-0014 (A/B against the last release); ADR-0016 records why.

## Tickets

- [01: stored expectations, and the tools to make and use them](issues/01-stored-expectations.md)
- [02: smoke.yml builds once, runs once, compares with the stored values](issues/02-smoke-once.md)
- [03: a workflow that measures and proposes new expected values](issues/03-update-workflow.md)
- [04: fail only at 2x the expected value](issues/04-two-x-tolerance.md)
