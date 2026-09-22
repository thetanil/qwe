# 16: Release and nightly call smoke.yml

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 05

## What

- `release.yml`: a `smoke` job, `uses: ./.github/workflows/smoke.yml` with `exclude-tag: ${{ github.ref_name }}`.
  The web-UI flow publishes vX before the gates run, so "latest" would otherwise be vX itself.
  `publish` and `draft-on-failure` add `smoke` to their `needs`. Nothing else about publish changes
  (it builds with `--config=release` since 04).
- `nightly.yml`: `smoke`, needing `refresh-caches`, like the other gates.
- The drift test gets rule 6: release.yml and nightly.yml both call smoke.yml. Add fixture cases that
  delete each call and expect failure.
- README "Making a release" (step 4 lists smoke and perf among the gates) and `docs/ci-checks.md`
  "Nightly and release".

## Acceptance criteria

- [ ] Dropping the smoke call from either workflow fails the drift test: `unit: tools/ci/workflows_test.sh::release_calls_smoke`, `unit: tools/ci/workflows_test.sh::nightly_calls_smoke`
- [ ] The repo as committed passes: `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] A pre-release tag (`v0.3.0-rc1` after a version bump) runs smoke and perf with the right baseline (not itself) and publishes: `manual: push the tag; record the run URL and the baseline tag shown in the summary`
- [ ] A smoke failure in a release turns a web-UI release back into a draft: `manual: covered by the draft-on-failure path; verify once on an rc`
- [ ] `bazel test //...` green

## Comments
