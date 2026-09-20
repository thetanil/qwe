# 09: Close the remaining C coverage gaps

Status: resolved
Category: enhancement
Type: task
Blocked by: none (07 owns the allocation-failure paths; leave those to it)

## What

Ticket 05 measured coverage and closed the gaps in its own table only.
`docs/coverage.md` has the baseline. Lines missed in `src/`, after 05
(command: `bazel coverage //... --combined_report=lcov`):

| File | Missed / found | Note |
|---|---|---|
| `src/kernel/workflow.c` | 182 / 1161 | the run loop; by far the largest |
| `src/kernel/luaexec.c` | 62 / 268 | remainder after 05: pipe, fork and alloc failures |
| `src/kernel/luacbor.c` | 50 / 308 | CBOR<->Lua conversion, malformed input |
| `src/cli/encrypt/encrypt.c` | 45 / 50 | almost untested: 5 lines hit |
| `src/kernel/validate.c` | 44 / 248 | see 05 for what was dismissed |
| `src/kernel/proc.c` | 25 / 59 | |
| `src/secrets/keyfile.c` | 24 / 95 | key file permissions, bad formats |
| `src/edge/yaml/transcode.c` | 16 / 299 | |
| `src/secrets/envelope.c` | 13 / 71 | |
| the rest | under 10 each | `luafs`, `jobs`, `redact`, `trace`, `lifecycle`, `alloc`, `keygen`, `luavm`, ... |

The goal is the one from 05: every error path we can reach from a test has one.
Work file by file, biggest first. For each uncovered line, either a test reaches
it or the ticket says why not (defensive, allocation failure owned by 07, dead
code). Delete dead code rather than dismissing it, where that is safe.

## Acceptance criteria

- [x] `encrypt.c`: refusing argv secrets, an unreadable key, a write failure, each with its exit code. `e2e` or `unit`, whichever reaches it.
- [x] `workflow.c`: every uncovered line has a test or a written reason; grouped by what it does, not line by line.
- [x] `proc.c`, `keyfile.c`, `envelope.c`, `luacbor.c`, `transcode.c`, `luaexec.c`: same.
- [x] Every remaining `src/` file with a miss is listed in `docs/coverage.md` with a reason, so a later report can be compared to it.
- [x] `docs/coverage.md` has a third table, after 09.

## Comments

2026-09-20. `docs/coverage.md`, "After quality/09", has the third table and, for `workflow.c`,
a table by what the code does. Result: 85.1% of lines (5083 of 5974); `encrypt.c` 5 -> 45 of 50 hit,
`keyfile.c` 71 -> 91 of 95, `luacbor.c` 258 -> 297 of 306, `proc.c` 34 -> 56 of 60,
`workflow.c` 979 -> 1048 of 1169.

The biggest single change was to the measurement, not to the tests: a step child ends in `execvp` or
`_exit`, which discarded its gcov counters, so ~130 lines of `workflow.c`, `proc.c` and `luaexec.c`
looked untested. `src/kernel/gcov.h` dumps and resets the counters first (coverage builds only,
`-DQWE_GCOV` from `.bazelrc`; gated on `QWE_LUA_COVERAGE`, which `oom_test` unsets because a dump
allocates). Dumping from `qwe_oom_probe`'s forked child was tried and did nothing, so the allocation
paths that only `oom_test` reaches stay uncovered in the report; the table says which lines those are.

New tests: 13 e2e cases (the harness gained an optional `stdin` file), unit tests in `keyfile_test`,
`proc_test`, `lua_cbor_test`, `limits_test` and the new `luaexec_test`. Deleted dead code: the
`idx < 0` branch of `encode_table` in `luacbor.c`. Not deleted, and why: `escape_token` and
`last_token` in `validate.c` (a pointer would silently be wrong if the schema ever allowed `/` or `~`).

Not done: the `oom_shim.c` lines, which are test support. `tools/luacov/floor.txt` is updated to the
new counts. `docs/ci-checks.md` no longer lists this ticket as open.
