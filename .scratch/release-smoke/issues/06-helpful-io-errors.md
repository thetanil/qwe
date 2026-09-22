# 06: Helpful I/O errors in file.ensure, and the negative-case pattern

Status: resolved
Category: enhancement
Type: task
Blocked by: 05

## What

When `file.ensure` cannot write, today's message carries shell noise:
`file.ensure: cannot write x: sh: 1: cannot create x: Permission denied`. Make every failed command in the
file plugins one clean line: `<plugin>: cannot <op> <path>: <OS reason>`. The OS reason is the part of
stderr after the last `: `, trimmed, and it falls back to the whole trimmed stderr if there is none.
The helper is a local function in the plugin, a few lines long. `file.read` (07) and `file.line` (09)
copy it, and there is no shared module: the one-liner is cheaper to repeat than a new embedded module.

Add the **negative-case pattern** to `smoke.yml`, which later tickets repeat. A negative smoke workflow
is `tests/smoke/neg_<case>.yml`. The GitHub step that runs it has an `id`, `continue-on-error: true`, and
tees qwe's stdout+stderr to a file. The next step fails the job unless
`steps.<id>.outcome == 'failure'` and the file contains the expected message (`grep -F`).

The first negative, `neg_file_ensure_denied.yml`: a `run:` step makes a directory and `chmod 555`s it,
then `file.ensure` writes into it. It must fail with
`file.ensure: cannot write <path>: Permission denied`. The runner user is not root, so the permission
applies.

## Acceptance criteria

- [x] Write into a read-only directory: exactly the one-line message, exit 1, step `failed`/`plugin-error`: `e2e: tests/e2e/file_ensure_write_denied/` (skipped as root, like the ulimit cases)
- [x] chmod on a file the user does not own: `file.ensure: cannot chmod <path>: Operation not permitted`: `e2e: tests/e2e/file_ensure_chmod_denied/`
- [x] Stderr parsing (noise stripped, trailing newline, no colon, empty stderr): `plugin: plugins/builtin/file.ensure/test.lua::error_message_clean`
- [ ] smoke.yml runs `neg_file_ensure_denied.yml` with continue-on-error, and the job passes only because the assertion step saw failure plus the message: `manual: push; confirm in the run that the neg step is orange (failed, continued) and the assert step green` (not run, see comments)
- [ ] Making the negative workflow succeed (for example `chmod 755`) fails the job: `manual: workflow_dispatch on a scratch branch; record the run URL` (not run, same reason)
- [x] `bazel test //...` green; the coverage floor holds

## Comments

- `plugins/builtin/file.ensure/plugin.lua` gained a local `clean_reason(stderr)`: trims
  whitespace, then takes the text after the last `": "` (falling back to the whole trimmed
  string when there is none). Applied to all three of its error sites (`cannot read`,
  `cannot write`, `cannot chmod`). Confirmed end to end with the real binary against a
  chmod-555 directory: the message went from `file.ensure: cannot write x: sh: 1: cannot
  create x: Permission denied` to `file.ensure: cannot write x: Permission denied`.
- Two new e2e harness markers, generalizing the existing root-skip idea beyond `ulimit`:
  `needs-non-root` (skip as root — a permission check root ignores) and `needs-sudo` (skip
  unless `sudo -n true` works — setup.sh needs it to make a root-owned fixture).
  `file_ensure_chmod_denied` needs both: `needs-sudo` so `setup.sh` can create a file owned by
  root, and `needs-non-root` because if the harness itself were root, its own `chmod` would
  also succeed and the negative case would not be negative.
- `file_ensure_chmod_denied`'s fixture: `sudo sh -c 'printf hi > owned-by-root.txt'; sudo
  chmod 666 owned-by-root.txt`. The workflow's `file.ensure` step wants the same content
  (so the write half of `apply` never runs — only `chmod` does) but a different mode, so the
  only thing that can fail is the `chmod`, and it does, with exactly the expected message.
- `smoke.yml` gained the negative-case pattern as a template for tickets 07–14 to repeat:
  a `continue-on-error: true` step with an `id`, teeing `qwe run`'s output to
  `$RUNNER_TEMP/<case>.out`, followed by an assertion step checking
  `steps.<id>.outcome == 'failure'` and `grep -F`ing the expected message out of that file.
  `tests/smoke/neg_file_ensure_denied.yml` is the first case; `smoke_workflows_test.sh`
  already skips `neg_*.yml` (built in 05 for exactly this).
- **The two GitHub-only manual criteria are not checked off**, for the same reason as in 05:
  this session does not push or dispatch workflows
  (`thetanil/qwe/CLAUDE.md`'s "**Never push.**"). What's verified instead: the exact commands
  the `neg_file_ensure_denied` GitHub step runs (validate, then `run` piped through `tee`)
  were run by hand against the real binary and produced exit 1 and the expected message on
  stdout, which is what the assertion step's `grep -F` looks for.
- `bazel test //...`: 227 passed, 3 skipped (pre-existing), 0 failed. Coverage floor holds.
