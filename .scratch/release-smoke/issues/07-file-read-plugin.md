# 07: file.read plugin

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 06

## What

A built-in step plugin that reads a file on the job's target, through the execution backend (so it
works over ssh too), and exposes it as step outputs:

- `with: path` (required).
- Outputs (none secret): `exists` (`true`/`false`), `content`, `sha256`, `mode` (4-digit octal),
  `owner` (user name).
- It changes nothing: `check` reports nothing to do, so the step is always `unchanged`. The work is
  done in `check`, which runs once.
- A missing file is **not** an error: `exists=false` and the other outputs empty. A file that exists but
  cannot be read fails with the 06 message form (copy 06's local helper): `file.read: cannot read <path>: Permission denied`.
- The content is passed back intact, trailing newline included.

Register it the way the README's "Writing a plugin" / coverage section describes: in the modules
table, its schema, and its Lua test. Add a GitHub negative `neg_file_read_denied.yml` (a `chmod 000`
file) to smoke.yml.

## Acceptance criteria

- [ ] An existing file: all five outputs are right, and a later `run:` step echoes them: `e2e: tests/e2e/file_read_outputs/`
- [ ] A missing file: `exists=false`, success: `e2e: tests/e2e/file_read_missing/`
- [ ] An unreadable file: the one-line error, `failed`/`plugin-error`: `e2e: tests/e2e/file_read_denied/` (skipped as root)
- [ ] Command shapes and parsing against the recording backend (path quoting, a stat failure, a sha256sum failure): `plugin: plugins/builtin/file.read/test.lua::outputs`, `::missing`, `::denied`
- [ ] The summary shows file.read as `unchanged`: `e2e: tests/e2e/file_read_outputs/` (summary.md golden)
- [ ] The smoke.yml negative for an unreadable file passes: `manual: push; check the run`
- [ ] `bazel test //...` green; the coverage floor holds (new file listed with `--update`)

## Comments
