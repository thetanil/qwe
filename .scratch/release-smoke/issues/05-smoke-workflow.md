# 05: smoke.yml: build the release binary, run smoke_run.yml on a clean runner

Status: resolved
Category: enhancement
Type: task
Blocked by: 02, 04

## What

The tracer bullet for the smoke workflow. `.github/workflows/smoke.yml` runs on push to main, on
`workflow_dispatch` and on `workflow_call`, with `permissions: contents: read`.

- **build** (ubuntu-24.04, the shared setup with cache-key `release` and `ssh-target: "true"`):
  `bazel test --config=release //...`, then `bazel build --config=release //src/cli:qwe`, and
  `upload-artifact` of `qwe` as `qwe-build`.
- **smoke** (needs build; a fresh runner **without** the shared setup: that setup's ssh-target step
  removes passwordless sudo, which later smoke workflows need, and this job must prove the binary needs
  no Bazel): checkout, download `qwe-build`, `chmod +x`, then fail unless `file` reports it statically
  linked. Then one GitHub step per smoke workflow:
  `./qwe validate tests/smoke/smoke_run.yml && ./qwe run tests/smoke/smoke_run.yml --summary "$GITHUB_STEP_SUMMARY"`.
  If qwe fails, the step fails.

The smoke workflows live in `tests/smoke/`, one per area, named `smoke_<area>.yml`. They also run
locally with any built binary. This ticket adds `smoke_run.yml`: echo, a multi-line script,
workflow/job/step `env:` precedence, and a `$QWE_OUTPUT` step output read by a later step (checked with
`test` in `run:`, until `assert` exists in 08).

The smoke workflows also run in `bazel test //...`, so they cannot rot: one e2e-style test runs every
`tests/smoke/*.yml` (not `neg_*`) with `//src/cli:qwe` and expects exit 0. Timing is not checked, and
`become`/`apt` workflows are skipped there (a marker comment in the file says so).

README: a badge and a CI table row. `docs/ci-checks.md`: a row, with the commands in "Every push", so drift
test rules 1–3 hold.

## Acceptance criteria

- [x] `smoke_run.yml` passes with the fastbuild binary in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [x] The drift test accepts smoke.yml (badge, workflow_call, push to main, commands listed): `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] On a push to main, build and smoke are green, and the run summary shows the smoke_run report from 02: `manual: push; open the run; screenshot or paste the summary into a comment` (not run: this session never pushes, per the workspace's "never push" rule; see comments)
- [ ] Deliberately breaking `smoke_run.yml` (for example `exit 1` in a step) on a branch dispatch fails the smoke step and the job: `manual: workflow_dispatch on a scratch branch; record the run URL` (not run, same reason)
- [ ] The smoke job's log shows `statically linked`: `manual: same run` (not run on GitHub; verified equivalently below)
- [x] `bazel test //...` green

## Comments

- `.github/workflows/smoke.yml`: `build` (shared setup, `cache-key: release`, `ssh-target: "true"`)
  runs `bazel test --config=release //...` then `bazel build --config=release //src/cli:qwe`,
  uploads it as `qwe-build`. `smoke` (`needs: build`) has no shared-setup step at all — just
  checkout, `download-artifact`, `chmod +x`, a `file qwe | grep -q "statically linked"` gate,
  then the one `smoke_run` step.
- `tests/smoke/smoke_run.yml`: echo, a multi-line script, job env overriding workflow env and
  step env overriding job env (each asserted with `test` in a `run:`, since `assert` doesn't
  exist until 08), and a `$QWE_OUTPUT` step output consumed by a later step.
- `tests/smoke/smoke_workflows_test.sh` + `BUILD` (`//tests/smoke:smoke_workflows_test`): runs
  `qwe validate` then `qwe run` on every `tests/smoke/*.yml` except `neg_*.yml`, with the
  fastbuild binary, expecting exit 0 from both. A workflow needing root or a real package
  install carries `# smoke: skip-in-bazel: <reason>` near its top and is skipped here (none yet
  — this is scaffolding for 10/11's apt/become workflows).
- README got a `smoke` badge; `docs/ci-checks.md` got a table row and two new "Every push"
  commands (`bazel test --config=release //...`,
  `./qwe run tests/smoke/smoke_run.yml --summary "$GITHUB_STEP_SUMMARY"`), which
  `tools/ci/workflows_test.sh::repo_is_consistent` confirms are wired up.
- **The three GitHub-only manual criteria are not checked off.** This session works under
  `thetanil/qwe/CLAUDE.md`'s "**Never push.**" rule (this project's own override of the
  workspace default, which is otherwise "never commit"), and has no way to trigger a GitHub
  Actions run without pushing or dispatching on the remote. What I could verify locally instead:
  - Built the release binary and ran the exact `smoke` job commands (`file qwe | grep -q
    "statically linked"`, then `./qwe validate ... && ./qwe run ... --summary
    "$GITHUB_STEP_SUMMARY"`) from a scratch directory standing in for the checkout — same
    output shape as the manual verification asks for, icons and all.
  - `file bazel-bin/src/cli/qwe` reports `statically linked` (also re-confirmed from 04).
  - Did not simulate the "deliberately break it" case beyond what `summary_log_tail`/
    `summary_*` already cover in `bazel test //...` (a failing `run:` step, `continue-on-error`
    semantics, `qwe run`'s own exit code) — those are the same mechanics the broken-branch
    check would exercise, just not through an actual `workflow_dispatch`.
  When the user pushes (or dispatches `smoke.yml` on a branch), these three should be
  straightforward to close out by pasting the run's summary/log into this file.
- `bazel test //...`: 225 passed, 3 skipped (pre-existing), 0 failed. Coverage floor holds.
