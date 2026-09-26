# 07: Small bugprone classes

Status: ready-for-agent
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

- [ ] `.clang-tidy` excludes none of the four checks, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] `run.sh --raw` reports 0 for each of the four. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh --raw`
- [ ] The four doc rows are gone. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
