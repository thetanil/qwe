# 11: Smoke: become and secrets

Status: ready-for-human
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

- [x] `smoke_become.yml` passes on the runner: `manual: push; check the run` — confirmed on the real runner, see comments
- [x] `smoke_secrets.yml` passes, and the leak assertions pass: `manual: same run; paste the assertion step log into a comment` — confirmed on the real runner, log pasted in comments
- [ ] The leak assertion can fail: on a scratch branch, the generator also writes the plaintext into a `run: echo` line (not a secret, so qwe cannot know to redact it), and the assertion step fails: `manual: workflow_dispatch on a scratch branch; record the run URL` — still not done; no scratch-branch `workflow_dispatch` run exists yet, and see comments for a finding that complicates this criterion's premise before anyone spends a run on it
- [x] The secrets template and its generator step are covered in the Bazel suite by the same generator script run with the fastbuild binary: `e2e: tests/smoke:smoke_workflows_test`
- [x] `bazel test //...` green

## Comments

This project's CLAUDE.md says **never push**, so this ticket was left `ready-for-human`
after implementation. The user has since pushed (commit `2b48ab1`, [run 35865874382](
https://github.com/thetanil/qwe/actions/runs/35865874382)), and both `debug-smoke` and
`smoke` are green — `smoke_become` and `smoke_secrets` both passed in each:
[`debug-smoke`](https://github.com/thetanil/qwe/actions/runs/35865874382/job/107197180374),
[`smoke`](https://github.com/thetanil/qwe/actions/runs/35865874382/job/107198012432).
That closes the first two boxes for real, not just from this devcontainer's local
verification. The `smoke_secrets` step's actual log from `debug-smoke` (identical in
`smoke`):

```
qwe keygen: wrote a new key to /tmp/tmp.Nn51vs5HVt/home/.config/qwe/secret (mode 0600)
[secrets] env says ***
[secrets] ***
PASS: smoke_secrets.yml (no leak found)
```

Both the `env:`-sourced and the `with:`-sourced secret paths show `***`, matching the
local run and the design. `smoke_become`'s steps have no stdout of their own (everything
goes through `$QWE_OUTPUT` or files, never `echo`), so its log is empty; its ten steps all
show as passed in the job's step list, which is the only signal a `run:`-step-free job has
to show.

Still open: criterion 3, the intentional-leak sanity check. It needs a deliberate,
temporary change to `gen_secrets.sh` on a scratch branch and a `workflow_dispatch` run,
which is a genuinely separate, exploratory action from "push what's already committed" —
see the finding below before spending that run on it, since the most literal reading of
the ticket's leak ("the same value, echoed raw") does not actually demonstrate a gap.

**smoke_become.yml**: `become: true` (root) worked as expected for both a `run:` step
writing `$QWE_OUTPUT` and for `file.ensure`, but `become: nobody` combined with
`$QWE_OUTPUT` does not — `workflow.c`'s `make_output_file` creates that file `0600`,
owned by the user qwe itself runs as, *deliberately* ("so a secret it writes there is
never readable by another user"). A `become:`'d-down step running as a genuinely
different, unprivileged user (unlike `become: true`, which is root and bypasses all of
that) can never write to it. Worked around it rather than treating it as a bug: a prior
`become: true` step `mkdir -m 777`s a scratch directory, the `become: nobody` step writes
a plain file into it (`id -un > file`, no `$QWE_OUTPUT` involved), and a later,
non-`become`'d `file.read` + `assert` reads it back and checks both its content and its
`owner`. Verified for real (`qwe run ... --summary`): all ten steps succeed, including
both assert steps for the `nobody` case.

**smoke_secrets.yml.in + gen_secrets.sh**: the committed template is never run directly
(`ENVELOPE` is not a valid envelope — confirmed `qwe validate` rejects it with "the
envelope is too short"). `gen_secrets.sh <qwe-binary> <smoke-dir>` does everything the
ticket describes: fresh `$HOME`, `qwe keygen`, a random 64-hex-char value (`/dev/urandom`
via `od`, so it can never collide with literal text elsewhere), `qwe encrypt`, substituted
for `ENVELOPE` via `sed`, run, then four checks (summary, `result.json` +
`lifecycle.trace`, job log for the value's absence, job log for the presence of `***`).
Verified for real, both directly (`tests/smoke/gen_secrets.sh bazel-bin/src/cli/qwe
tests/smoke`) and via the Bazel suite (`tests/smoke:smoke_workflows_test`, which now
calls it as its last step, per criterion 4) — both the `env:` and `with:`-sourced secret
paths show up as `***` in the job log, and none of the four checked artifacts have the
plaintext.

**Criterion 3, unresolved, and a finding**: I tried to reproduce "the generator writes the
plaintext into a `run: echo` line and the assertion fails" locally (not committed — a
throwaway variant of the script) by appending a `run: echo "leaking $value"` step to the
*generated* workflow, using the *same* `$value` that is also `TOKEN`'s plaintext. It did
**not** leak — qwe redacted that line too (`***leaking ***`), because redaction is a
straight string match against every decrypted secret value found anywhere in captured
output, not a taint tracked through `${{ secrets.* }}` templating specifically. So a
`run: echo` of the *same* secret value doesn't demonstrate a real gap — the value would
need to reach the log via a string qwe never decrypted as a secret in that run (a second,
undeclared value), which is a different scenario than "the generator [...] writes the
plaintext into a run: echo line" as literally written, and I'm not certain what the
ticket author had in mind. Whoever picks this up on a scratch branch should decide what
the intended leak actually is before spending a `workflow_dispatch` run on it.
