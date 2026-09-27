# 12: Swappable parameters in tests

Status: ready-for-agent
Category: enhancement
Type: task

## What

9 findings in `*_test.c` (same check, same rules as ticket 11; tests get the same
checks as the rest of the code, `docs/static-analysis.md`):

- `write_file(name, body)` in `validate/oom_test.c:21`, `load_oom_test.c:42`,
  `oom_test.c:25` (`path, body`), `workflow_test.c:27`;
- `has_file(dir, name, want)` in `oom_test.c:62`;
- `run(script, input, input_len, use_pipe, out, ...)` in `preamble_test.c:16` (2
  findings: `script`/`input`, and a convertible pair);
- `spawn_with_fd_room(room, p, int *open_before, int *open_after)`, `proc_test.c:311`;
- `stat_group_session(line, long *pgrp, long *sid)`, `proc_test.c:88`.

Round 2 argued a swap "fails the test at once". A test that fails at once is the
good outcome, but the test author still loses time, and an assessor will not
distinguish test code from product code in a check that is on everywhere.

## Fix

Duplicate helpers are the cheapest win: the four `write_file`s are the same
function. One `qwe_test_write_file(const struct qwe_test_file *f)` (path, body) in
`src/testing/` (or the name/body pair passed as a `struct qwe_test_file`
literal) removes four findings and four copies, and is used by ticket 03's new
boundary tests too. `run`, `has_file`, `stat_group_session` and
`spawn_with_fd_room`: the same struct or out-struct treatment as ticket 11.

## Acceptance criteria

- [ ] With the check enabled locally (ticket 11 removes the exclusion, this one
      does not touch `.clang-tidy`), the gate reports no
      `easily-swappable-parameters` finding in a `*_test.c`. `manual: run.sh`
- [ ] One shared `write_file` in `src/testing/`; the four private copies are gone.
      `manual: grep -rn "static void write_file" src`
- [ ] No exclusion, option or `NOLINT`. `manual: git diff .clang-tidy`
- [ ] `bazel test //...` is green, same test count.

## Comments
