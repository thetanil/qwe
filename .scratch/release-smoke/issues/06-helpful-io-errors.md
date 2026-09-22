# 06: Helpful I/O errors in file.ensure, and the negative-case pattern

Status: ready-for-agent
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

- [ ] Write into a read-only directory: exactly the one-line message, exit 1, step `failed`/`plugin-error`: `e2e: tests/e2e/file_ensure_write_denied/` (skipped as root, like the ulimit cases)
- [ ] chmod on a file the user does not own: `file.ensure: cannot chmod <path>: Operation not permitted`: `e2e: tests/e2e/file_ensure_chmod_denied/`
- [ ] Stderr parsing (noise stripped, trailing newline, no colon, empty stderr): `plugin: plugins/builtin/file.ensure/test.lua::error_message_clean`
- [ ] smoke.yml runs `neg_file_ensure_denied.yml` with continue-on-error, and the job passes only because the assertion step saw failure plus the message: `manual: push; confirm in the run that the neg step is orange (failed, continued) and the assert step green`
- [ ] Making the negative workflow succeed (for example `chmod 755`) fails the job: `manual: workflow_dispatch on a scratch branch; record the run URL`
- [ ] `bazel test //...` green; the coverage floor holds

## Comments
