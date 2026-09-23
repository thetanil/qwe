# 16: Release and nightly call smoke.yml

Status: resolved
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

- [x] Dropping the smoke call from either workflow fails the drift test: `unit: tools/ci/workflows_test.sh::release_calls_smoke`, `unit: tools/ci/workflows_test.sh::nightly_calls_smoke`
- [x] The repo as committed passes: `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] A pre-release tag (`v0.3.0-rc1` after a version bump) runs smoke and perf with the right baseline (not itself) and publishes: `manual: push the tag; record the run URL and the baseline tag shown in the summary` (not run: this session never pushes; see comments)
- [ ] A smoke failure in a release turns a web-UI release back into a draft: `manual: covered by the draft-on-failure path; verify once on an rc` (not run, same reason)
- [x] `bazel test //...` green

## Comments

- `release.yml`: a new `smoke:` job (`uses: ./.github/workflows/smoke.yml`, `with: exclude-tag:
  ${{ github.ref_name }}`) added to `publish`'s and `draft-on-failure`'s `needs:`, right next to
  the other five gate jobs. `exclude-tag` stops the perf job's own "newest eligible release"
  search (15) from finding this release as its own baseline, since the web-UI publish flow makes
  the tag's release exist (as a public draft-turned-live release) before any gate has run.
  Nothing about `publish`'s own build/package/attach steps changed.
- `nightly.yml`: a `smoke:` job needing `refresh-caches`, `uses: ./.github/workflows/smoke.yml`,
  at its defaults (no `exclude-tag`, since nightly runs off `main` and there is no tag being
  published to exclude) -- same shape as the other five `needs: refresh-caches` gate calls.
- `tools/ci/workflows_test.sh` rule 6: `nightly.yml` and `release.yml` must each contain
  `uses: ./.github/workflows/smoke.yml`. Two new fixture cases, `release_calls_smoke` and
  `nightly_calls_smoke`, delete that line from a scratch copy of each workflow (the same
  `sed -i "/smoke.yml/d"` pattern rule 4's cases use for the five gates) and expect `check_repo`
  to fail; both are now in the default case list alongside `repo_is_consistent`, which runs the
  same check against the workflows as committed.
- README "Making a release" step 4 and `docs/ci-checks.md`'s "Nightly and release" section now
  mention `smoke.yml` (and its perf sub-job) among what nightly and release run, and note the
  `exclude-tag` reasoning inline in `ci-checks.md`.
- Verified `release.yml` and `nightly.yml` still parse as well-formed YAML with a standalone
  libyaml-based checker built from the vendored `third_party/libyaml` sources (no python), the
  same one used to check 15's `smoke.yml` edits.
- **The two manual criteria are not checked off**, same reason as 05 and 15: no push access to
  a real tag from this session, under `thetanil/qwe/CLAUDE.md`'s "Never push" rule. What was
  verified locally: `bazel test //...` green including the new drift-test cases, and the workflow
  files' YAML syntax.
- `bazel test //...`: 248 passed, 3 skipped (pre-existing), 0 failed.
