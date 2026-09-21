# 01: The manual checks the ci feature left open

Status: ready-for-human
Category: task
Type: task
Blocked by: none

## What

The `ci` feature was archived (`.scratch/archive/ci/`) with its automated criteria passing and
these manual ones not yet checked. Archived tickets are not edited, so they are tracked here.
Each needs GitHub or a throwaway branch, which is why an agent did not do them.

## Acceptance criteria

- [ ] A dispatched `nightly` runs all five gates and `fuzz` green, and the next push to `main` logs "Cache hit" for the caches it saved; the nightly badge renders. `manual: gh workflow run nightly.yml; push; read the log` (archived ticket 12)
- [ ] `fuzz.yml`: `seconds=300` builds, fuzzes, saves the corpus cache and shows four processes in the summary; a second dispatch restores the corpus (compare the `INITED` lines); `seconds=30000` is rejected before building; a push does not start it; the badge renders. `manual: gh workflow run fuzz.yml -f seconds=300` (archived ticket 08)
- [ ] A one-hour fuzz run's executions, corpus and coverage are recorded in `docs/fuzzing.md`. `manual: copy from the nightly's or a dispatched run's summary` (archived ticket 08)
- [ ] The `v0.2.0` release's `qwe` prints `qwe 0.2.0`, is static (`file qwe`), and `qwe validate` runs on a workflow from `tests/e2e/` on a machine without the repo; `gh attestation verify` succeeds if provenance is kept; the release badge renders. `manual: download the release assets` (archived ticket 09)
- [ ] A tag that does not match `QWE_VERSION` (`v9.9.9-rc0` on a throwaway commit) fails before publishing and creates no release. `manual: push it; delete the tag after` (archived ticket 09)
- [ ] A deliberate failure turns each gate red and leaves its artifact: a failing test (`tests`, ticket 01), a leak (`asan`, 03), a signed overflow (`ubsan`, 04), an uninitialised read (`valgrind`, 05), an untested function under `src/` (`coverage`, 06, and the HTML report still shows it). `manual: a throwaway branch with the trigger temporarily widened`
- [ ] In the devcontainer `bazel test //...` is still green with the ssh cases running against the real host. `manual: run it there` (archived ticket 02)

## Comments
