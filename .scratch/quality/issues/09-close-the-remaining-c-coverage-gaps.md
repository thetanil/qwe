# 09: Close the remaining C coverage gaps

Status: ready-for-agent
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

- [ ] `encrypt.c`: refusing argv secrets, an unreadable key, a write failure, each with its exit code. `e2e` or `unit`, whichever reaches it.
- [ ] `workflow.c`: every uncovered line has a test or a written reason; grouped by what it does, not line by line.
- [ ] `proc.c`, `keyfile.c`, `envelope.c`, `luacbor.c`, `transcode.c`, `luaexec.c`: same.
- [ ] Every remaining `src/` file with a miss is listed in `docs/coverage.md` with a reason, so a later report can be compared to it.
- [ ] `docs/coverage.md` has a third table, after 09.

## Comments
