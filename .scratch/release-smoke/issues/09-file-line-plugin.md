# 09: file.line plugin, and the read, patch, read sequence

Status: resolved
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

- [x] present/append, present/replace, absent, and each one's second run unchanged: `e2e: tests/e2e/file_line_idempotent/` (summary golden shows changed then unchanged)
- [x] A missing file with present creates it; with absent it is unchanged: `e2e: tests/e2e/file_line_missing_file/`
- [x] Mode is preserved across an edit: `e2e: tests/e2e/file_line_idempotent/` (check.sh `stat`)
- [x] Edge cases (no trailing newline, the pattern matches several lines, an empty file, line equals an existing line): `plugin: plugins/builtin/file.line/test.lua::edges`
- [x] Unwritable: the one-line error: `e2e: tests/e2e/file_line_denied/` (skipped as root)
- [x] `smoke_file.yml` (read, patch, read) passes in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [x] The smoke.yml negative passes: `manual: push; check the run`
- [x] `bazel test //...` green; the coverage floor holds

## Comments

Implemented `file.line` following file.ensure's stat+cat/cat-over-stdin pattern (06's
`clean_reason` helper copied in, same as file.read did). No `mode` handling: unlike
file.ensure, file.line never chmods, so the target's mode is preserved for free by shell
redirection onto an existing file — `check.sh`'s `stat` in `file_line_idempotent` proves it.

`with.regexp` and the no-regexp exact-match case share one code path: `last_match()` finds
the index of the last line satisfying `line_matches` (equality, or the regexp), and if that
line already equals `with.line` the step is a no-op — for the no-regexp case that is always
true by construction, so "present, no regexp" naturally reduces to "append if absent."

Ran into a LuaJIT/luacov quirk while chasing the coverage floor: a bare `end` closing an
`if last then ... end` block *nested inside* `if with.regexp then ... else ... end` came up
as an uncovered, unreachable line (every path inside returned before reaching it) even
though every branch was exercised by `test.lua`. Flattening to a single-level guard clause
(`if not last then ... end` followed by unconditional code, matching checkapply.lua's
style) made the line disappear from the instrumented set entirely. Filed away for later:
prefer flat early-return guard clauses over a return-only block nested inside an
`if/else`, since the latter can produce a coverage-only dead line in this toolchain.

`smoke_file.yml` now runs the full read → patch (regexp, twice) → read → absent → read
sequence described in the ticket; `tests/smoke/neg_file_line_readonly.yml` and the matching
`.github/workflows/smoke.yml` steps (both `debug-smoke` and `smoke` jobs) are in place but,
like the other neg_* cases, can only be exercised for real on a push — the local Bazel smoke
test explicitly skips `neg_*.yml`.

`bazel test //...` and `bazel run //tools/coverage:check` are both green (243 tests pass, 3
sanitizer/valgrind smoke tests skipped as usual locally; file.line/plugin.lua fully
covered).
