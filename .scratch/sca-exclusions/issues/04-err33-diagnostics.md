# 04: `cert-err33-c` on diagnostics; delete `CheckedFunctions`

Status: resolved
Category: enhancement
Type: task
Blocked by: 03

## What

The other 90 of the 173 hidden `cert-err33-c` findings are `fprintf`/`fputs`
to `stderr`. 69 are in production code: `workflow.c` (30), `bcembed.c` (7),
`encrypt.c` (6), `validate.c` (6), `keygen.c` (5), and one to three in 10 more
files. 21 are in tests: `oom_test.c` (8), `validate/oom_test.c` (5),
`lifecycle_test.c` (4), `sites_oom_test.c` (2), `load_oom_test.c` and
`encrypt/oom_test.c`.

A failed write to `stderr` has no one to report to. Ignoring it is correct,
but that should be one decision written in one place, not 90 unexplained calls.
The shape `sca-findings/05` chose for `snprintf` (`qwe_msg` in `fmt.h`) fits
here as well. Add a `qwe_diag(const char *fmt, ...)` (the name is open) that
writes to `stderr` and casts its one ignored return, with a comment saying why.
Replace the bare calls with it. Keep the output byte-for-byte the same: the e2e
goldens compare `stderr`.

Once 03 and this ticket are done, the raw run has no `cert-err33-c` finding
left under its default function list. Then delete `CheckedFunctions` from
`.clang-tidy`, so the check runs with its full default list. Removing the
narrowing is what closes it; adding three more names to it does not.

## Acceptance criteria

- [x] `.clang-tidy` has no `cert-err33-c.CheckedFunctions`, and the gate exits 0. `manual: grep -n CheckedFunctions .clang-tidy finds nothing; CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] `run.sh --raw` reports 0 `cert-err33-c` findings. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh --raw`
- [x] Every e2e golden still matches, so no diagnostic changed. `e2e: tests/e2e/` (the whole suite, under `bazel test //...`)
- [x] The helper has a unit test. `unit: src/kernel/fmt_test.c::<diag case>` (or wherever the helper lives)
- [x] The `cert-err33-c` row in `docs/static-analysis.md` is gone, and the diagnostic idiom is documented next to "snprintf: say what a cut means". `manual: docs/static-analysis.md`
- [x] `bazel test //...` and the coverage check are green. `unit: bazel test //...`; `manual: bazel run //tools/coverage:check`

## Comments

`qwe_diag(const char *fmt, ...)` lives in `src/kernel/put.h`, next to 03's
`qwe_out_*` (one header for "a write whose failure is checked once, or has
nobody to tell"). It is `(void)vfprintf(stderr, ...)` with a comment saying why.
All 94 `fprintf(stderr, ...)` sites in `src/` and `tools/` (production and
tests, including `fmt.h`'s truncation abort and `alloc.c`'s OOM message) became
`qwe_diag`, so the bytes written are unchanged. `dispatch.c` and `run.c`'s
usage text, which were `fputs(..., stderr)` and a multi-line `fprintf`, became
`qwe_diag` too. The three new BUILD deps came from adding `:put` to every
target that compiles one of the 22 files, plus `fmt` (which now includes it).

Unit test: `//src/kernel:put_test` covers `qwe_diag` writing to fd 2 (fd 2
redirected to a file), surviving a closed stderr, and the three `qwe_out_*`
(including a failure landing in `ferror`).

`.clang-tidy` has no `CheckOptions` block now. `run.sh --raw` reports 0
`cert-err33-c` findings and a total of 256 (254 + 2 new findings in classes
still excluded: a `_POSIX_C_SOURCE` define in `put_test.c` and the
`_GNU_SOURCE` define that `summary_test.c` needed for `fopencookie`); "hidden by
a CheckOptions narrowing: 0". Gate exit 0, `bazel test //...` 258 pass and 3
skipped (includes every e2e golden), coverage check: every file at least 85%.
