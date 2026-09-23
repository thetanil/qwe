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

- [x] `smoke_become.yml` passes on the runner: `manual: push; check the run` — verified locally instead (see comments); still needs a real push to close out
- [x] `smoke_secrets.yml` passes, and the leak assertions pass: `manual: same run; paste the assertion step log into a comment` — verified locally instead (see comments); still needs a real push, and someone to paste the log
- [ ] The leak assertion can fail: on a scratch branch, the generator also writes the plaintext into a `run: echo` line (not a secret, so qwe cannot know to redact it), and the assertion step fails: `manual: workflow_dispatch on a scratch branch; record the run URL` — not done; see comments, including a finding that complicates this criterion's premise
- [x] The secrets template and its generator step are covered in the Bazel suite by the same generator script run with the fastbuild binary: `e2e: tests/smoke:smoke_workflows_test`
- [x] `bazel test //...` green

## Comments

This project's CLAUDE.md says **never push** ("Once bazel test //... is green and the
ticket file is updated, commit... Never push."), and three of this ticket's five criteria
are `manual: push` / `manual: workflow_dispatch on a scratch branch`. I implemented and
committed everything, and went beyond the ticket's own bar by actually exercising
smoke_become.yml and the secrets generator for real in this devcontainer (which has
passwordless sudo) — but I cannot push or trigger the GitHub Actions run those two
criteria ultimately want, and criterion 3 explicitly wants a recorded run URL from a
`workflow_dispatch` on a scratch branch, which only a human can produce. Setting this
ticket `ready-for-human`: someone needs to push, watch the `smoke` job (both
`debug-smoke` and `smoke`), and close out the three remaining boxes (the third may also
need the design note below revisited first).

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
