# 04: Pin clang-tidy, run it on every push and PR, keep evidence

Status: ready-for-human
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

- [x] CI runs the pinned major version and `run.sh` refuses another.
      `manual: run.sh with CLANG_TIDY=clang-tidy-18 exits non-zero naming the pin`
- [x] The gate runs on push, pull request, nightly, release and by hand.
      `unit: tools/ci/workflows_test.sh` (rule updated, `repo_is_consistent` green)
- [x] `docs/ci-checks.md` and `docs/static-analysis.md` state the version, the
      trigger and the artifact. `manual: both docs`
- [ ] A run uploads the evidence artifact, and a release attaches it.
      `human: needs a push, which this project's agents never do (CLAUDE.md).
      The agent stops with everything else done, and sets Status:
      ready-for-human with the steps; the human pushes, links the run in the
      Comments, and closes the ticket`
- [x] The gate at the pinned version is green on `main`. If pinning changes any
      finding (18 vs 20 differences the tickets never saw), fix each in code;
      list them in the Comments. `manual: run.sh at exit 0`
- [x] `bazel test //...` is green.

## Comments

- Everything that doesn't need a push is done. What's left is entirely the
  human step in AC 4 (below).

- **The pin.** `tools/clang-tidy/pin.env` is the one file: `CLANG_TIDY_MAJOR=20`
  (what `run.sh` enforces locally, against whatever `$CLANG_TIDY` resolves to)
  and `CLANG_TIDY_VERSION=20.1.8` + `CLANG_TIDY_URL` + `CLANG_TIDY_SHA256`
  (what CI installs and verifies). The URL is LLVM's own GitHub release asset,
  `LLVM-20.1.8-Linux-X64.tar.xz` (`llvmorg-20.1.8`) — the only pre-built Linux
  binary LLVM ships for this version; there's no smaller `clang+llvm-*-linux-gnu`
  split any more. Checked the sha256 against the release's own sigstore
  provenance (`.jsonl` asset) before pinning it, and again by downloading and
  hashing the 2 GB file directly: both agree on
  `1ead36b3dfcb774b57be530df42bec70ab2d239fbce9889447c7a29a4ddc1ae6`.
  Went with the tarball over a container image pinned by digest: no image with
  this exact clang-tidy build already exists, and building/pushing one is more
  moving parts than a single verified download.

- **Why not apt.llvm.org.** Confirmed the concern in the ticket: the devcontainer's
  `clang-tidy-20` comes from `deb https://apt.llvm.org/noble/
  llvm-toolchain-noble-20 main`, and `apt-cache policy` shows only one candidate
  version for that suite (today, `20.1.8`). There's no version pin that survives
  apt.llvm.org moving the suite to `20.1.9` and dropping `20.1.8`'s `.deb`s from
  the pool, so CI would silently drift off the version every count in this file
  was measured with. A URL + sha256 doesn't drift.

- **Why only the binary.** `ldd` on the release build's `bin/clang-tidy` shows it
  needs nothing from the 2 GB tarball at runtime beyond `libm`/`libz`/`libc`
  (the runner's own) — clang-tidy statically links the rest of LLVM into itself.
  `.github/actions/clang-tidy-pin` extracts just that one file (~170 MB) instead
  of the whole tree, and caches it by `<version>-<sha256>` (a key that never
  changes, like the setup-bazel disk caches already in this repo), so only the
  very first run on a fresh cache generation pays for the download.

- **No 18-vs-20 findings.** The devcontainer's `clang-tidy-20` is already exactly
  `20.1.8` — the same point release the pin names — so there was nothing to
  reconcile: `tools/clang-tidy/run.sh` (unpinned CI's actual gap) still exits 0
  on `main` with the pin check added. The 18-vs-20 drift the ticket warned about
  was real (CI ran plain `apt: clang-tidy`, i.e. 18, while every count in this
  file was measured at 20.1.8), but it never produced a finding CI missed and a
  human didn't already fix — it just meant CI's "green" wasn't provably this
  repo's green. Confirmed the negative case works the other way: `CLANG_TIDY=
  clang-tidy-18 tools/clang-tidy/run.sh` refuses immediately, naming the pin
  file, without ever reaching Bazel.

- **Evidence.** `run.sh --evidence-dir DIR` (new flag, same script — one place
  that knows the file list, not a second copy of the aquery logic in the
  workflow) writes `version.txt`, `.clang-tidy`, `files.txt`, `sha.txt`,
  `output.txt` (full, not `--quiet`) and `exit_status.txt`. `static-analysis.yml`
  uploads that directory as `clang-tidy-evidence-<sha>` on every run, pass or
  fail (`if: always()`). `release.yml` downloads that same artifact in the
  `publish` job and zips it into `dist/clang-tidy-evidence-<version>.zip`,
  attached to the release alongside the binaries — a release asset doesn't
  expire the way a workflow artifact does.

- **Cadence and PR gating.** Moved the job out of `valgrind.yml` into its own
  `static-analysis.yml` (own badge, own cadence) rather than folding it into
  `tests.yml`: it needs its own pinned-toolchain setup step, which would be an
  odd fit bolted onto the plain test job. Added `pull_request:` to every gate
  that already ran on every push (tests, asan, ubsan, coverage, smoke) as well
  as the new static-analysis.yml — not to valgrind, which stays nightly/
  release/manual only (23 minutes is too slow to gate a PR on, and the ticket's
  own cadence complaint was about static analysis's ~1-minute cost, not
  valgrind's). `tools/ci/workflows_test.sh` rule 3 now requires the
  `pull_request` trigger on every workflow but valgrind (new case
  `pull_request_missing`), and rule 4's gate list grew a sixth member,
  `static-analysis`. `nightly.yml` and `release.yml` both call it now too.

- **What's left (AC 4, human).** Push this commit to `main` (or open the PR —
  either way `static-analysis.yml` will run). Steps once it has:
  1. Confirm the `static-analysis` run is green and uploaded a
     `clang-tidy-evidence-<sha>` artifact.
  2. Link that run here.
  3. At the next tagged release, confirm `release.yml`'s `publish` job attached
     `clang-tidy-evidence-<version>.zip` to the release.
  4. Set `Status: resolved` and tick this last box.
