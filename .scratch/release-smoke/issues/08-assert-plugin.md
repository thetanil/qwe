# 08: assert plugin, and smoke_file.yml (ensure, read, assert)

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 07

## What

A built-in step plugin that lets a workflow check its own results:

- `with: actual` (string, required), exactly one of `equals` / `contains` / `matches` (a Lua pattern),
  and an optional `message`.
- It runs **no** command on the target. `check` does the comparison. On a match it reports nothing to do
  (the step is `unchanged`). On a mismatch it raises a plugin error:
  `assert: <message or "values differ">: expected <op> <expected>, actual <actual>`, with long values cut
  at 200 characters. Redaction applies as usual, so a secret compared with `assert` shows as `***`.
- The schema's `oneOf` refuses zero or more than one operator at `qwe validate`.

Add `tests/smoke/smoke_file.yml`: `file.ensure` writes a file, `file.read` reads it back, and `assert`
checks `exists`, `content`, `mode` and `sha256` (against a known hash). Then it runs `file.ensure` again
(unchanged), `file.read` + `assert` again. Add `neg_assert_mismatch.yml` to smoke.yml, with the expected
message grep.

Convert `smoke_run.yml`'s `test` checks to `assert` steps where one fits.

## Acceptance criteria

- [ ] equals/contains/matches pass, and each one fails with the message form above: `e2e: tests/e2e/assert_ops/`, `e2e: tests/e2e/assert_mismatch/`
- [ ] Zero or two operators are refused by `qwe validate`, with the schema path: `e2e: tests/e2e/validate_assert_one_operator/`
- [ ] It runs no backend command: `plugin: plugins/builtin/assert/test.lua::no_commands` (recording backend records nothing)
- [ ] An asserted secret is redacted in the error: `e2e: tests/e2e/assert_secret_redacted/`
- [ ] `smoke_file.yml` passes in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [ ] The smoke.yml negative for a mismatch passes: `manual: push; check the run and its summary`
- [ ] `bazel test //...` green; the coverage floor holds

## Comments
