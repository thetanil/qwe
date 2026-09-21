# CI: GitHub Actions for the quality gates, fuzzing and releases

## Goal

`docs/ci-checks.md` lists every check the `quality` feature built, with the exact
command, and says "There is no CI service configured yet". This feature wires
those checks into GitHub Actions on `thetanil/qwe`:

1. **Every push to `main` runs the full quality gate**: every check in
   `docs/ci-checks.md` except fuzzing. The "nightly" cadence in that doc goes
   away. UBSan and both valgrind runs move to every push. The measured costs
   (valgrind about 130 s, the rest seconds to minutes when warm) make that
   affordable.
2. **Each quality target is its own workflow, and has its own README badge.**
   GitHub's status badge is per workflow file, so "one badge per target" means
   one workflow file per target. The README shows a row of badges: tests, asan,
   ubsan, valgrind, coverage, fuzz and release.
3. **Fuzzing is its own workflow, started only by hand** (`workflow_dispatch`),
   with the duration as an input. It is never part of a push or a release,
   because a useful run takes hours.
4. **A release** is a pushed `v<version>` tag. It runs the whole quality gate
   again at that commit, builds the shipping binary, checks that the tag matches
   the version compiled into it, and publishes a GitHub Release with the binary
   and checksums.

## Workflows

| File | Badge | Triggers | Runs |
|---|---|---|---|
| `tests.yml` | tests | push to main, `workflow_dispatch`, `workflow_call` | `bazel test //...` |
| `asan.yml` | asan | same | `bazel test --config=asan //...` |
| `ubsan.yml` | ubsan | same | `bazel test --config=ubsan //...` |
| `valgrind.yml` | valgrind | same | `bazel test --config=valgrind //...` and `bazel test //tests/e2e:valgrind_e2e` |
| `coverage.yml` | coverage | same | `bazel run //tools/coverage:check`, and the HTML report as an artifact |
| `fuzz.yml` | fuzz | `workflow_dispatch` only | `tools/fuzz/nightly.sh <seconds>` |
| `release.yml` | release | push of a tag `v*` | the five gate workflows via `workflow_call`, then build and publish |

The gate workflows share one setup, a local composite action
(`.github/actions/setup/`): the runner image, Bazel at `.bazelversion`, the apt
packages a job needs, and a Bazel disk cache keyed per config.

## Decisions taken while writing the tickets

- **Runner:** `ubuntu-24.04`. Its valgrind package is 3.22, the version
  `docs/valgrind.md` measured. The devcontainer image lives in `/workspace`, not in
  this repo, so CI cannot reuse it. The first ticket proves that the plain suite,
  including `-Werror`, passes on the runner's gcc.
- **Bazel:** bazelisk, which is preinstalled on GitHub's images and reads
  `.bazelversion`. It is never an unpinned install.
- **Superseded runs:** each workflow has a `concurrency` group per ref with
  `cancel-in-progress: true` on `main` pushes, but never on a release tag.
- **The ssh e2e cases must run in CI, not skip.** 19 cases carry `needs-ssh` and
  skip unless `REMOTE_CONTAINERS` is set and `172.18.0.1` answers ssh. Skipped,
  they leave the ssh backend uncovered, and the coverage floor, which was measured
  in the devcontainer with them running, would fail. Ticket 02 gives the runner
  an ssh target.
- **Guard against drift:** a Bazel test (`//tools/ci:workflows_test`, ticket 01)
  fails when a command in `docs/ci-checks.md` has no workflow running it, or a
  workflow has no README badge. It runs locally in `bazel test //...`, so drift is
  caught before a push.
- **The agent never pushes** (`CLAUDE.md`). Each ticket's `manual:` criteria are
  checked by the user after pushing: the run is green, the badge renders, and the
  failure path was seen to fail. A ticket is `resolved` only after they are
  ticked.

## Out of scope

- Pull-request triggers. The project pushes to `main` directly; adding
  `pull_request:` later is one line per workflow.
- Scheduled (cron) fuzzing. The user chose manual dispatch.
- MSan (`docs/sanitizers.md` says why), and fuzzing libyaml or LuaJIT.
- Architectures other than linux x86_64 for the release binary.
- Step containment and the `step-containment` feature's cgroup tests. Whether a
  GitHub runner can delegate a cgroup is that feature's question.

## Open questions

- **Coverage percentage badge (ticket 07).** The coverage workflow's badge shows
  pass or fail against the floor. A percentage badge needs somewhere to publish a
  number from CI. Ticket 07 uses GitHub Pages, which also hosts the HTML report.
  Mark it `wontfix` if the pass/fail badge is enough.
- **Provenance.** `actions/attest-build-provenance` on the release binary is cheap
  to add, and ticket 09 includes it. Drop it if it is not wanted.
