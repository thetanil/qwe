# 06: String-to-number conversion and errno

Status: resolved
Category: bug
Type: task
Blocked by: 01

## What

**`cert-err34-c` (6 hits).** `src/kernel/workflow.c:1854` does
`ctx.grace_ms = grace_env ? atol(grace_env) : 10000;`, so a malformed
`QWE_TEST_GRACE_MS` silently becomes 0 ms of grace. The other five are in
`src/kernel/proc_test.c` (57, 135, 143, 205, 243), which parses
`/proc/<pid>/stat` and child output. The doc calls that input trusted, but a
test that misparses still passes with a wrong value. Use
`strtol`/`strtoul` with end-pointer and `errno` checks.

**`clang-analyzer-unix.Errno` (3 hits).** `src/kernel/summary.c:122`,
`src/kernel/summary_test.c:24` and `src/edge/yaml/corpus_test.c:24`. The doc
says the `summary.c` hit "traced into an unrelated loop with no `errno` in the
flagged file". It is still there, and it says `errno` "may be overwritten by
function 'malloc'" after an `ftell`/`fseek`. Re-read it with the full
`--raw` path output before accepting that explanation. It sits next to the
`ftell()` bug the first run found.

## Acceptance criteria

- [x] A malformed `QWE_TEST_GRACE_MS` is rejected or ignored with a diagnostic, never parsed as 0. `e2e: tests/e2e/grace_env_malformed/`
- [x] `.clang-tidy` no longer excludes `cert-err34-c` or `clang-analyzer-unix.Errno`, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] The `summary.c:122` verdict (real bug, or a `NOLINTNEXTLINE` with the traced reason) is recorded in the comments. `manual: this ticket's ## Comments`
- [x] Both rows are gone from the exclusion table, and real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

- **`summary.c:122`: real bug.** The earlier "unrelated loop" explanation was wrong. With `clang-analyzer-*` on, the path is `qwe_summary_write` → `log_tail`, and the note says: "After calling 'rewind' reading 'errno' is required to find out if the call has failed". `rewind` returns nothing, so a failed rewind went unseen and the `fread` that followed read from wherever the stream was. The hit is the same at `acb416d`, `0eb7f1c` and after ticket 04. Fix: `rewind` is replaced with a checked `fseek(fp, 0, SEEK_SET)`. The other two hits, `summary_test.c`'s `slurp` and `corpus_test.c`'s `replay`, were the same `rewind` and got the same fix. No `NOLINT`.
- Why a single-check run finds nothing: `unix.Errno` depends on `unix.StdCLibraryFunctions` to model which calls set `errno`. `--checks='-*,clang-analyzer-unix.Errno'` reports 0 hits. The gate runs all of `clang-analyzer-*`, so it sees them.
- `cert-err34-c`: `workflow.c`'s `atol(QWE_TEST_GRACE_MS)` is now `strtol` with end-pointer, `ERANGE` and negative checks. A bad value prints `qwe run: warning: ignoring QWE_TEST_GRACE_MS=<v>: not a whole number of milliseconds` and keeps the 10 s default. The e2e case `grace_env_malformed` (`3OO`) was red before the fix and green after. `grace_then_sigkill` and `grace_outlives_leader` still pass.
- `proc_test.c`: its five `sscanf`/`atoi` calls now go through `whole_number` (a strict `strtol` of a whole string, optionally ending in a newline) and `stat_fields` (the state and n numeric fields after the last `)` of a `/proc/<pid>/stat` line). A line of the wrong shape is -1, not a partly-filled result.
- Side effect for the next feature: there is now no `rewind` in `src/` or `tools/`, so the `bugprone-unsafe-functions`/`cert-msc24-c`/`cert-msc33-c` row, whose justification is "only ever `rewind()`", should have nothing left to flag.
- Gate exits 0. `bazel test //...` is green (252 pass, 3 skipped).
