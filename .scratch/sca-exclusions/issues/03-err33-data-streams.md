# 03: `cert-err33-c` on data streams

Status: resolved
Category: bug
Type: task
Blocked by: 01

## What

Of the 173 `fprintf`/`fputs`/`fputc` findings that `CheckedFunctions` hides,
83 write data to a file, not a diagnostic to `stderr`:

| File | Sites | What it writes |
|---|---|---|
| `src/kernel/result.c` | 37 | `result.json` |
| `src/kernel/summary.c` | 28 | the run summary (`$GITHUB_STEP_SUMMARY`-style markdown) |
| `tools/bcembed.c` | 7 | the embedded module table |
| `src/kernel/workflow.c` | 2 | `report_disabled`'s memstream (already only printed after a clean `fclose`, `sca-findings/04`) |
| `src/cli/dispatch.c` | 1 | usage text to the `out` stream |
| tests | 8 | fixture files: `summary_test.c` (3), `oom_test.c` (2), `validate/oom_test.c`, `load_oom_test.c`, `workflow_test.c` |

Checking every `fprintf` of a JSON writer is not the fix. A stdio stream's
error flag is sticky, so the idiom is: write freely, then check
`ferror(fp)` once before `fclose`, and check `fclose` too. `result.c` already
ends with `return ferror(fp) ? -1 : 0;`. `bcembed` does
`bad = ferror(out); if (fclose(out) != 0 || bad)`.

**Suspected real bug:** `qwe_summary_write` (`summary.c`) only checks
`fclose(fp)`. It never calls `ferror`. `fclose` reports a failure of its own
final flush, not an earlier failed write, so a summary that lost a block in
the middle and then flushed its tail cleanly is reported as written. The
existing `write_failure_after_open_fails` test writes to `/dev/full`, where
every write fails, including the final flush, so it cannot tell the
difference. Prove it before fixing: write a summary larger than the stdio
buffer to a stream whose first write fails and later ones succeed (e.g. a
`fopencookie`/`funopen` stream with a failing write callback, or `--wrap=write`
in the test), and show it returns 0.

Then find one way to make the analyzer accept the sticky-flag idiom without 83
`(void)` casts. For example, the per-call writes go through small `static`
helpers that return `void`, where each helper casts once and says why
(`/* ferror(fp) is checked before fclose */`). Or `result.c` and `summary.c`
share a writer in `src/kernel/`. Pick one and use it for all three writers.
Test fixture writers follow `sca-findings/03`'s idiom (a fixture that fails to
write aborts).

## Acceptance criteria

- [x] `summary.c`'s write-error handling is decided and recorded: a real bug with a red-then-green test, or a comment proving why `fclose` alone is enough. `unit: src/kernel/summary_test.c::<a new mid-stream write failure case>`
- [x] Every writer that ignores a per-call return checks `ferror` before `fclose`, and a comment at the ignore says so. `manual: grep -n ferror src/kernel/result.c src/kernel/summary.c tools/bcembed.c`
- [x] With `fprintf`, `fputs` and `fputc` added to `CheckedFunctions`, the gate reports no finding in any data-stream site listed above. `manual: add the three to CheckedFunctions locally; CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh names only stderr sites (ticket 04's); revert or leave for 04`
- [x] Real bugs are listed under "What re-enabling excluded checks found". `manual: docs/static-analysis.md`
- [x] `bazel test //...` and the coverage check are green. `unit: bazel test //...`; `manual: bazel run //tools/coverage:check`

## Comments

**The suspected `summary.c` bug is real.** `qwe_summary_write` only checked
`fclose`. To reach a mid-stream failure the body moved into
`qwe_summary_render(FILE *, ...)` (the path version is that plus the open and
the close, the same split as `qwe_result_write`). `summary_test`'s
`mid_stream_write_failure_fails` writes 80 jobs to a `fopencookie` stream whose
first write fails and every later one succeeds. Red first, with the render
returning 0 after the writes: `fclose` returned 0 and the render reported
success, failing on `-1 != rc`. Green once it ends with `return ferror(fp) ? -1 : 0`.

**Shape chosen:** `src/kernel/put.h` (`//src/kernel:put`, public so
`tools/bcembed` can use it) with `qwe_out_str`, `qwe_out_ch` and `qwe_out_fmt`.
Each returns nothing and carries the one `(void)` cast with a comment saying
`ferror(fp)` is checked before `fclose`. `result.c` (already ended in `ferror`),
`summary.c` and `bcembed.c` (already did `bad = ferror(out)`) use them, as do
the five test fixture writers (`write_file` in `workflow_test`, `oom_test`,
`load_oom_test`, `validate/oom_test`, and the log fixtures in `summary_test`,
which now `ASSERT(!ferror(fp))` before `fclose`).

`report_disabled`'s memstream also checks `ferror` now, not just `fclose`.

**Corrections to the ticket's table:** the `src/cli/dispatch.c` site is the
usage text to `stderr`, not an `out` stream, so it stays for 04. With
`fprintf`, `fputs` and `fputc` added to `CheckedFunctions` locally, the gate's
remaining `cert-err33-c` findings are all `stderr` sites (I checked each
finding's source line; the only one without `stderr` on its first line is
`dispatch.c`'s multi-line `fputs(..., stderr)`). Reverted the local edit.

Gate exit 0, `bazel test //...` 257 pass and 3 skipped, coverage check: every
file at least 85%.
