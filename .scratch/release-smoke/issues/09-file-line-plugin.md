# 09: file.line plugin, and the read, patch, read sequence

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 06, 07, 08

## What

A built-in step plugin that edits one line of a file on the target, idempotently:

- `with: path`, `line`, `state` (`present`|`absent`, default `present`), and an optional `regexp`
  (a Lua pattern).
  - `present` without regexp: append `line` if no line equals it.
  - `present` with regexp: replace the last matching line with `line`, or append it if none matches.
  - `absent`: remove every line equal to `line` (or matching `regexp` if given).
- `check` reads the file through the backend and decides. `apply` writes the new whole content through
  stdin (as `file.ensure` does), preserving the file's mode. A missing file with `present` is created
  with `line`; a missing file with `absent` is already in state.
- A trailing newline is kept (a file without one gets one when a line is appended).
- I/O failures use the 06 message form (copy 06's local helper).

`smoke_file.yml` becomes the full sequence:
1. ensure a config file;
2. read it and assert;
3. `file.line` patches a key (changed), then the same patch again (unchanged);
4. read it and assert the new content;
5. `file.line` absent (changed), read it and assert the line is gone.

Add a `neg_file_line_readonly.yml` negative (a file in a `555` directory, written by the runner user).

## Acceptance criteria

- [ ] present/append, present/replace, absent, and each one's second run unchanged: `e2e: tests/e2e/file_line_idempotent/` (summary golden shows changed then unchanged)
- [ ] A missing file with present creates it; with absent it is unchanged: `e2e: tests/e2e/file_line_missing_file/`
- [ ] Mode is preserved across an edit: `e2e: tests/e2e/file_line_idempotent/` (check.sh `stat`)
- [ ] Edge cases (no trailing newline, the pattern matches several lines, an empty file, line equals an existing line): `plugin: plugins/builtin/file.line/test.lua::edges`
- [ ] Unwritable: the one-line error: `e2e: tests/e2e/file_line_denied/` (skipped as root)
- [ ] `smoke_file.yml` (read, patch, read) passes in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [ ] The smoke.yml negative passes: `manual: push; check the run`
- [ ] `bazel test //...` green; the coverage floor holds

## Comments
