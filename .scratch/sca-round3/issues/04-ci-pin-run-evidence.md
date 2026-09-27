# 04: Pin clang-tidy, run it on every push and PR, keep evidence

Status: ready-for-agent
Category: bug
Type: task

## What

Four separate gaps between what `docs/static-analysis.md` says and what CI does.

1. **The version.** `valgrind.yml`'s `static-analysis` job installs
   `apt: clang-tidy`. On ubuntu-24.04 that resolves to clang-tidy 18
   (`apt-cache policy clang-tidy`: candidate `1:18.0-59~exp2`). Every count and
   every option in rounds 1 and 2 was measured at clang-tidy 20.1.8
   (`CLANG_TIDY=clang-tidy-20`). The analyzer's checkers, the check set and some
   options differ between 18 and 20, so CI's "green" is not this repo's green.
   The doc does not state a version anywhere.
2. **The cadence.** Static analysis runs "nightly, on release and by hand, never
   on a push", because it shares `valgrind.yml`'s cadence. The gate takes about a
   minute (58 s measured locally, warm). A finding therefore reaches `main` and
   waits up to a day.
3. **No pre-merge check.** `tests.yml` triggers on `push: branches: [main]` and
   `workflow_dispatch` only. There is no `pull_request` trigger on any gate
   workflow, so nothing is checked before a change lands. (A qualification argument wants the check to precede the merge, whoever
   merges.)
4. **No evidence.** Nothing is stored. An assessor asks for the analyzer's
   version, its configuration, the file list it covered, and the result, for a
   given commit.

## Fix

- Install a pinned clang-tidy 20 in a new composite step under
  `.github/actions/`. Prefer a container image pinned by digest, or LLVM's
  release tarball pinned by URL and SHA-256: apt.llvm.org serves only the latest
  point release of each major, so "the apt repository at a stated version" does
  not stay reproducible. Pin the exact version (20.1.8, what the counts were
  measured with), not only the major; `run.sh` enforces the major locally so the
  devcontainer's clang-tidy-20 still works: it prints `clang-tidy --version`
  and fails if the major version is not the pinned one (`CLANG_TIDY_MAJOR` and
  the exact CI version in one file that the docs and the CI step both read).
- Move the job out of `valgrind.yml` into `static-analysis.yml` (or add it to
  `tests.yml`), on `push` to main, `pull_request`, `workflow_call` (so
  `nightly.yml` and `release.yml` call it), `workflow_dispatch`.
  `tools/ci/workflows_test.sh` rule 3 and `docs/ci-checks.md` change with it.
- Upload an artifact per run: `clang-tidy --version`, `.clang-tidy`, the file
  list, the git SHA, the full output (not `--quiet`), and the exit status.
  GitHub keeps workflow artifacts for at most 90 days (up to 400 on a private
  repo), which is probably shorter than evidence has to be kept
  (`.scratch/iso26262-tool-qualification` ticket 01 decides how long; this
  ticket does not wait for it). So on `release.yml` also attach the same bundle to the GitHub
  release as an asset, which does not expire.
- Add `pull_request` to every gate workflow.

## Acceptance criteria

- [ ] CI runs the pinned major version and `run.sh` refuses another.
      `manual: run.sh with CLANG_TIDY=clang-tidy-18 exits non-zero naming the pin`
- [ ] The gate runs on push, pull request, nightly, release and by hand.
      `unit: tools/ci/workflows_test.sh` (rule updated, `repo_is_consistent` green)
- [ ] `docs/ci-checks.md` and `docs/static-analysis.md` state the version, the
      trigger and the artifact. `manual: both docs`
- [ ] A run uploads the evidence artifact, and a release attaches it.
      `human: needs a push, which this project's agents never do (CLAUDE.md).
      The agent stops with everything else done, and sets Status:
      ready-for-human with the steps; the human pushes, links the run in the
      Comments, and closes the ticket`
- [ ] The gate at the pinned version is green on `main`. If pinning changes any
      finding (18 vs 20 differences the tickets never saw), fix each in code;
      list them in the Comments. `manual: run.sh at exit 0`
- [ ] `bazel test //...` is green.

## Comments
