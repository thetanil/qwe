# 05: smoke.yml: build the release binary, run smoke_run.yml on a clean runner

Status: ready-for-agent
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

- [ ] `smoke_run.yml` passes with the fastbuild binary in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [ ] The drift test accepts smoke.yml (badge, workflow_call, push to main, commands listed): `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] On a push to main, build and smoke are green, and the run summary shows the smoke_run report from 02: `manual: push; open the run; screenshot or paste the summary into a comment`
- [ ] Deliberately breaking `smoke_run.yml` (for example `exit 1` in a step) on a branch dispatch fails the smoke step and the job: `manual: workflow_dispatch on a scratch branch; record the run URL`
- [ ] The smoke job's log shows `statically linked`: `manual: same run`
- [ ] `bazel test //...` green

## Comments
