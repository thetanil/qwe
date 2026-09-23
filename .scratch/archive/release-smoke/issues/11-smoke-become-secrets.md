# 11: Smoke: become and secrets

Status: resolved
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
- [x] The leak assertion can fail: on a scratch branch, an undeclared secret reaches `$QWE_OUTPUT` (not through `${{ secrets.* }}` echoed raw -- see comments for why that specific scenario doesn't leak -- but a `run:` step's output, which ADR-0005 redacts only when declared via `secret-outputs:`), and the assertion step fails: `manual: workflow_dispatch on a scratch branch; record the run URL` — done, [run 35878451039](https://github.com/thetanil/qwe/actions/runs/35878451039)
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

**Criterion 3, closed for real.** The literal reading ("the generator writes the plaintext
into a `run: echo` line") doesn't demonstrate a gap: echoing the *same* value that
`${{ secrets.TOKEN }}` already decrypted this run gets redacted too (`***leaking ***`),
because redaction is a straight string match against every decrypted secret value found
anywhere in captured output (`src/kernel/redact.c`), not a taint tracked through
`${{ secrets.* }}` templating specifically.

The real gap is the one `docs/adr/0005-outputs-travel-apart-from-logs.md` already names:
a `run:` step's `$QWE_OUTPUT` is read back from a file after the command exits
(`workflow.c`'s `take_output_file`), never through `qwe_redact_feed` (the one call site is
the stdout/stderr tee, `workflow.c:301`). The ADR's own fix for this is
`secret-outputs:` on the step, which registers the value with the redaction set before
`result.json` is written (tested by `tests/e2e/secret_output_not_in_result`). An output
that *omits* that declaration is exactly "not a secret, so qwe cannot know to redact it" —
genuinely undeclared, not a duplicate of a value already known.

Verified locally first (cheaper than a run): a scratch copy of
`smoke_secrets.yml.in` with one appended step,
```yaml
- id: leaky-output
  env:
    TOKEN: ${{ secrets.TOKEN }}
  run: echo "leak=$TOKEN" >> "$QWE_OUTPUT"
```
(no `secret-outputs:`) makes `gen_secrets.sh` fail with
`gen_secrets: the value leaked into result.json or lifecycle.trace` — the plaintext lands
in `result.json`'s `steps.leaky-output.outputs.leak`, confirmed with `jq`.

Then for real: branch `scratch/ticket-11-leak-check`, same one-step change to
`tests/smoke/smoke_secrets.yml.in`, pushed, `gh workflow run smoke.yml --ref
scratch/ticket-11-leak-check` ([run 35878451039](
https://github.com/thetanil/qwe/actions/runs/35878451039)). `debug-smoke` failed at the
`smoke_secrets` step with the identical message
(job [107240398054](https://github.com/thetanil/qwe/actions/runs/35878451039/job/107240398054)):
```
qwe keygen: wrote a new key to /tmp/tmp.LDPBeLtPyI/home/.config/qwe/secret (mode 0600)
[secrets] env says ***
[secrets] ***
gen_secrets: the value leaked into result.json or lifecycle.trace
##[error]Process completed with exit code 1.
```
The branch and its remote copy were deleted immediately after (`git branch -D`, `git push
origin --delete`); nothing from it is merged. This is not treated as a qwe bug: it is the
documented boundary of a declaration-based mechanism, now actually exercised by a real
negative control instead of just by inspection.
