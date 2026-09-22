# 11: Smoke: become and secrets

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 05, 08

## What

Two more smoke workflows, run by smoke.yml with `--summary`:

- `smoke_become.yml` (marked as skipped in the Bazel smoke test: it needs passwordless sudo):
  - `run: id -u` with `become: true` writes an output, and `assert` checks it equals `0`;
  - `become: nobody` + `id -un`, and assert `nobody`;
  - `file.ensure` with `become: true` creates a root-owned file, and `file.read` + `assert` check that
    `owner` is `root`.
- `smoke_secrets.yml`. A preceding **GitHub** step runs, with `HOME` set to a temp dir,
  `./qwe keygen` and `printf %s "$VALUE" | ./qwe encrypt`, then writes the blob into a copy of the
  workflow template (`smoke_secrets.yml.in`). The blob is key-specific, so it cannot be committed. qwe
  then runs the generated workflow. In it, the secret reaches a step through `env:` and `with:`, and a step
  echoes it. After the run, a GitHub step asserts four things:
  - the summary written for this run has no plaintext;
  - the job log has `***`;
  - `result.json` and `lifecycle.trace` have no plaintext;
  - the process list is not checked, since e2e already covers argv.
  Use a random value per run, so a leak cannot hide behind a fixed string.

## Acceptance criteria

- [ ] `smoke_become.yml` passes on the runner: `manual: push; check the run`
- [ ] `smoke_secrets.yml` passes, and the leak assertions pass: `manual: same run; paste the assertion step log into a comment`
- [ ] The leak assertion can fail: on a scratch branch, the generator also writes the plaintext into a `run: echo` line (not a secret, so qwe cannot know to redact it), and the assertion step fails: `manual: workflow_dispatch on a scratch branch; record the run URL`
- [ ] The secrets template and its generator step are covered in the Bazel suite by the same generator script run with the fastbuild binary: `e2e: tests/smoke:smoke_workflows_test`
- [ ] `bazel test //...` green

## Comments
