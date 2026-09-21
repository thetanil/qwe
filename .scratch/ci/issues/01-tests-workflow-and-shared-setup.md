# 01: The tests workflow, the shared setup, and the drift test

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: none

## What

This is the first vertical slice: one workflow that runs `bazel test //...` on
every push to `main`, one badge for it in the README, and the pieces every later
workflow reuses.

**`.github/actions/setup/action.yml`**, a composite action:

- `ubuntu-24.04`, and Bazel through the preinstalled bazelisk, which reads
  `.bazelversion` (8.7.0). Fail the job if `bazel --version` is not 8.7.0.
- Input `apt:` is a space-separated package list, installed with
  `apt-get install --no-install-recommends` (empty for this workflow).
- Input `cache-key:` names the config. Restore and save a Bazel disk cache
  (`--disk_cache=~/.cache/bazel-disk`) with `actions/cache`, keyed on the runner
  OS, the config and `hashFiles('MODULE.bazel.lock', '.bazelrc')`, with a prefix
  restore key. Do not cache the output base.
- Write a `~/.bazelrc` with `--disk_cache` and
  `--test_output=errors`. Do not edit the repo's `.bazelrc` for CI.

**`.github/workflows/tests.yml`:** triggers are `push: branches: [main]`,
`workflow_dispatch` and `workflow_call` (the release, ticket 09, calls it).
`permissions: contents: read`. `concurrency: tests-${{ github.ref }}`, with
`cancel-in-progress` true except for tags. It runs `bazel test //...`. On
failure it uploads `bazel-testlogs/**/test.log` and `test.outputs/` as an
artifact.

**README:** a `## Status` section at the top with the tests badge
(`https://github.com/thetanil/qwe/actions/workflows/tests.yml/badge.svg?branch=main`,
linked to the workflow's runs page). Later tickets add their badges to the same
row, in the order shown in the spec's table.

**`//tools/ci:workflows_test`**, an `sh_test` in `bazel test //...`, reads
`.github/workflows/*.yml`, `README.md` and `docs/ci-checks.md` as data. Export them
with a `filegroup` or `exports_files`. `.github` is a normal Bazel package
directory. It fails when:

1. a workflow file has no badge in the README, or a README badge names a workflow
   file that does not exist;
2. a command in the "Every change" code block of `docs/ci-checks.md` appears in no
   workflow file (compare the exact command text);
3. a workflow other than `fuzz.yml` and `release.yml` lacks the `push` to `main`
   trigger or `workflow_call`.

Rule 2 starts with the "Every change" block. Ticket 06 renames that block, once
everything but fuzzing runs on every push.

**Runner gcc.** The devcontainer's gcc is not the runner's (13.x on 24.04). If
`-Werror` fires on the runner, fix the code, not the flags. If a test depends on
something the devcontainer has and the runner lacks, record it under Comments and
install it through `apt:`. Skipped ssh cases are expected here and are fixed by
ticket 02.

## Acceptance criteria

- [x] `workflows_test` fails when a workflow file has no README badge. `unit: tools/ci/workflows_test.sh::badge_missing`
- [x] It fails when a README badge names a workflow file that does not exist. `unit: tools/ci/workflows_test.sh::badge_dangling`
- [x] It fails when an "Every change" command in `docs/ci-checks.md` appears in no workflow. `unit: tools/ci/workflows_test.sh::command_unwired`
- [x] It fails when a gate workflow lacks the push-to-main trigger or `workflow_call`. `unit: tools/ci/workflows_test.sh::trigger_missing`
- [x] It passes on the repo as committed. `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] A push to `main` runs `tests.yml`, it is green, and it shows `bazel test //...` with the ssh cases printing SKIP. `manual: push; open Actions → tests; read the log`
- [ ] A second push with no source change finishes faster from the disk cache, and the log shows cache hits. `manual: push an empty commit; compare durations; grep the log for "disk cache hit"`
- [ ] A deliberately failing test turns the run red and uploads the test logs artifact. `manual: on a throwaway branch with the trigger temporarily widened, break an assertion, push, download the artifact, then delete the branch`
- [ ] The README's tests badge renders and links to the runs page. `manual: view README on github.com`

## Comments

- Implemented; unit criteria pass locally. The `manual:` criteria are checked from the first pushed run.
- ssh-target lives in the setup action already (ticket 02 builds on it); tests.yml sets it now.
- The "Every change" block in docs/ci-checks.md lists only `bazel test //...` until tickets 03 and 06 wire asan and coverage, so rule 2 stays green per commit.
