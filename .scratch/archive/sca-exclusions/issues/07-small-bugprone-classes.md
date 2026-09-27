# 07: Small bugprone classes

Status: resolved
Category: enhancement
Type: task

## What

Four exclusions have 16 findings between them. Each site is quicker to fix
than the exclusion is to justify.

**`bugprone-assignment-in-if-condition` (7).** The doc calls this "a
deliberate, common idiom in this codebase", but there are only seven sites:
`luacbor.c:57,63,88,91,114`, `proc_test.c:177` and `bcembed.c:70`. Assign,
then test.

**`bugprone-implicit-widening-of-multiplication-result` (6).**
`encrypt.c:33`, `limits_test.c:28,49`, `transcode.c:407`, `workflow.c:353,710`.
These are all constant macros like `QWE_YAML_MAX_SIZE` and `RESULT_MAX`
multiplied in `int`. Write the macros in `size_t`
(`((size_t)1024 * 1024)`), so the product is computed wide in the first
place. Don't cast at each use.

**`bugprone-misplaced-widening-cast` (2).** `luaexec_test.c:83` and
`proc_test.c:318`, `(rlim_t)(first + n)`. Cast before adding.

**`clang-analyzer-optin.performance.Padding` (1).** `lifecycle_test.c:226`,
`struct step`. Reorder its fields.

## Acceptance criteria

- [x] `.clang-tidy` excludes none of the four checks, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] `run.sh --raw` reports 0 for each of the four. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh --raw`
- [x] The four doc rows are gone. `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

All four exclusions are gone; 16 findings fixed, none of them a bug.

- **`assignment-in-if-condition` (7):** assign, then test. `luacbor.c` (5),
  `proc_test.c:177`, and `bcembed.c`'s `out = argc < 2 ? NULL : fopen(...)`.
- **`implicit-widening-of-multiplication-result` (6):** `QWE_YAML_MAX_SIZE`,
  `QWE_RING_CAPACITY`, `RESULT_MAX` and `MAX_PLAIN` are `((size_t)1024 * 1024)`
  and so on, so the product is computed wide. The two `%d` formats that
  printed one (`transcode.c`'s "document is larger than" and `encrypt`'s "longer
  than") became `%lu` with `(unsigned long)`; the `encrypt_too_long` e2e golden
  and `limits_test`'s "larger than" assertion still match. `QWE_YAML_MAX_DEPTH` is a small `int` used as a
  depth count, so `limits_test.c`'s `2 * QWE_YAML_MAX_DEPTH` index is done in a
  local `size_t depth` instead of widening the macro.
- **`misplaced-widening-cast` (2):** `(rlim_t)first + 2` and `(rlim_t)first + room`.
- **`Padding` (1):** `struct step` in `lifecycle_test.c` is widest field first.
  Its initializers were positional, in the `GO`, `GO_P` and `STALE` macros and
  in two hand-written `SCENARIO` lines (`skip_in_pending`, `skip_in_ready`,
  which the ticket did not mention). All now use designated initializers, so
  the order is free. Making that pass `bugprone-macro-parentheses` meant
  `PLAIN`, `COE`, `FAILS` and `CARRY` became compound literals, so a macro
  argument can be parenthesized.

`run.sh --raw` reports none of the four (the remaining rows are the excluded
`easily-swappable-parameters`, `multi-level-implicit-pointer-conversion`,
`msc30`/`msc32`, and the two options). Gate exit 0, `bazel test //...` 260
pass, 3 skipped, coverage check green.
