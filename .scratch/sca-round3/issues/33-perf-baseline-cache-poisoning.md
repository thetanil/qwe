# 33: `perf-baseline.yml` must not save a cache from the ref it measures

Status: ready-for-agent
Category: bug
Type: task

## What

Code scanning alert #11 (`actions/cache-poisoning/poisonable-step`, high),
`.github/workflows/perf-baseline.yml:38`. The `build` job checks out `inputs.ref` (any
branch, tag or commit the person dispatching types in) and then runs
`./.github/actions/setup`, whose `bazel-contrib/setup-bazel` step saves the Bazel disk,
repository and bazelisk caches on every event but `pull_request`. A `workflow_dispatch` run
saves into the default branch's cache scope, which every push to `main` and every gate then
restores. A ref whose build rules or sources are hostile can plant objects the gates reuse,
without ever passing a reviewed pull request (the `PR` ruleset's whole point).

Only someone with write access can dispatch the workflow, so the practical risk is small. It
is still a way onto `main`'s gates around the ruleset, and the alert stays open until it is
closed properly.

PR #9 (Copilot, closed) added a `cache-save` input to `.github/actions/setup` and set it to
`"false"` in `perf-baseline.yml`. CodeQL stops matching, but the hole stays:
`uses: ./.github/actions/setup` resolves from the workspace, and the workspace is the
checkout of `inputs.ref`. The untrusted ref supplies its own `action.yml` and can ignore the
input. Whether a cache is saved must not be decided by code from the measured ref.

## Fix

In `perf-baseline.yml`'s `build` job, nothing loaded from the checked-out ref may control
caching:

- call `bazel-contrib/setup-bazel` directly (pinned the same as in `.github/actions/setup`)
  with no disk cache and `cache-save: false`, instead of the local composite action; or
- check out the default branch's `.github/` into a separate path and `uses:` the setup
  action from there, with a cache-save switch it honours.

The first is simpler and, for a job that runs a few times a release, the cold build costs a
few minutes. The release binary it measures is arguably better built from scratch anyway.
Keep the "Bazel is the pinned version" check if the composite is dropped (a direct step
comparing `bazel --version` with `.bazelversion`).

The `measure` job also checks out `inputs.ref` and runs its `tools/perf/run.sh`, but it uses
no cache and has `contents: read`; `propose` checks out the default branch. Confirm both
while there, and say so in the Comments.

## Acceptance criteria

- [ ] No step in `perf-baseline.yml` that runs after the checkout of `inputs.ref` comes from
      the workspace (`uses: ./...`) or saves an Actions cache. `manual: read the workflow`
- [ ] The build job still pins Bazel to `.bazelversion`. `manual`
- [ ] Code scanning alert #11 is closed by the fix (not dismissed).
      `manual: gh api repos/thetanil/qwe/code-scanning/alerts/11 --jq .state`
- [ ] A manual `perf-baseline` run from the branch builds, measures and opens its proposal
      PR, and the Actions cache list shows no entry saved by that run.
      `manual: gh workflow run perf-baseline.yml --ref <branch> -f version=... ; gh cache list`
      (the proposal PR it opens is closed unmerged)
- [ ] `//tools/ci:workflows_test` passes (rule 7: perf-baseline stays dispatch-only and
      uncalled).

## Comments
