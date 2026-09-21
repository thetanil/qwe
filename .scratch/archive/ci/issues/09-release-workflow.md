# 09: The release workflow

Status: in-progress (manual criteria await a tag)
Category: enhancement
Type: task
Blocked by: 06

## What

`release.yml`, triggered by `push: tags: ['v*']`. A release is a tag. There is no
manual release button, so what is released is always a commit in the history.

**1. Gate.** Call the five gate workflows with `uses: ./.github/workflows/<x>.yml`:
tests, asan, ubsan, valgrind and coverage. Each must pass at the tag's commit.
Fuzzing is not part of the gate. The gates ran on this commit already when it
was pushed to `main`, but the release runs them again. That costs minutes, and
the release does not depend on a cache or on the push history.

**2. Version check.** The version lives in one place, `QWE_VERSION` in
`src/kernel/qwe.h` (`0.1.0` today). The job fails unless the tag is exactly
`v` + `QWE_VERSION` and the built binary's `qwe --version` prints `qwe <QWE_VERSION>`.
Put the check in a script (`tools/release/check_version.sh <tag> <qwe binary>`),
so it is tested locally. Release notes and `--version` then never disagree, and
you cannot tag `v0.2.0` on a tree that still says 0.1.0.

**3. Build.** `bazel build //src/cli:qwe //src/cli:qwe-debug`. These are the same
targets the e2e cases run, static, with `qwe` stripped (README, "The binary").
Check `file` in the log: statically linked, x86-64. Package them as
`qwe-<version>-linux-x86_64` and `qwe-debug-<version>-linux-x86_64`, and write
`SHA256SUMS`.

**4. Publish.** `gh release create <tag> --verify-tag --generate-notes` with the
two binaries and `SHA256SUMS`. `permissions: contents: write` goes on this job
only. The gate jobs keep `contents: read`. A tag with a pre-release suffix
(`v0.2.0-rc1`) is published with `--prerelease`, and check_version accepts
`QWE_VERSION` with the same suffix.

**5. Provenance** (the spec's second open question). Run
`actions/attest-build-provenance` on both binaries, with
`permissions: id-token: write, attestations: write`, on the publish job. Remove
this step if the user declines it at triage.

Add the release badge. It is the workflow badge without `?branch=`, because the
workflow runs on tags.

Also remove the README's "# up next / ci to release" section, which this
feature finishes.

## Acceptance criteria

- [x] `check_version.sh` passes when the tag and `qwe --version` match `QWE_VERSION`. `unit: tools/release/check_version_test.sh::match`
- [x] It fails, naming both values, when the tag differs from `QWE_VERSION`. `unit: tools/release/check_version_test.sh::tag_mismatch`
- [x] It fails when the binary's `--version` differs, using a stub binary. `unit: tools/release/check_version_test.sh::binary_mismatch`
- [x] It accepts a matching pre-release suffix and refuses a tag without the `v`. `unit: tools/release/check_version_test.sh::prerelease_and_prefix`
- [x] `workflows_test` accepts `release.yml` without a push-to-main trigger, and checks that it calls all five gate workflows. `unit: tools/ci/workflows_test.sh::release_calls_every_gate`
- [ ] Pushing `v0.1.0` runs all five gates, then builds and publishes a release with `qwe`, `qwe-debug` and `SHA256SUMS`, and the downloaded `qwe --version` prints `qwe 0.1.0`. `manual: git tag v0.1.0 && push the tag; download; sha256sum -c SHA256SUMS; ./qwe --version`
- [ ] The downloaded `qwe` is static, and `qwe validate` runs on a workflow from `tests/e2e/` on a machine without the repo. `manual: file qwe; ./qwe validate <copied case>`
- [ ] A tag that does not match `QWE_VERSION` fails before publishing, and no release is created. `manual: push v9.9.9-rc0 on a throwaway commit; confirm red and no release; delete the tag`
- [ ] `gh attestation verify qwe-0.1.0-linux-x86_64 --repo thetanil/qwe` succeeds, if provenance is kept. `manual: after the v0.1.0 release`
- [ ] The release badge renders. `manual: view README on github.com`

## Comments

- Implemented as specified, with one change (user): the release is normally written in the web UI, so `release.yml` also accepts a release that already exists for the tag: binaries are uploaded to it, the commit hash is appended to its notes, and if a gate or the version check fails it is turned back into a draft. Provenance (`attest-build-provenance`) is not included; add it if wanted. `workflows_test` checks `release_calls_every_gate`; `//tools/release:check_version_test` covers the version script. The manual criteria need a real tag.
