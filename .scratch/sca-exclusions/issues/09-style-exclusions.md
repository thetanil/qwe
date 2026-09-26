# 09: Re-verify the two style exclusions

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

These two are the most likely to stay excluded. The work is to prove that
with numbers, or to find an option that narrows the check.

**`bugprone-multi-level-implicit-pointer-conversion` (50).** Doc: "fires on
every `calloc`/`free` call". A classification at `2d56de5` agrees: `free` 27,
`realloc` 10, `calloc` 5, `qsort` 3 (plus two `qsort` comparators doing
`const char *const *x = a`), `qwe_xcalloc` 2, `malloc` 1. They are spread
over `luaexec.c` 15, `luacbor.c` 9, `run.c` 7, `workflow.c` 7, `luafs.c` 5,
`validate.c` 5 and `jobs.c` 2. The check has no options. Confirm that every
site is a `void *` to `T **` conversion through an allocator or `qsort`. If
so, close `wontfix` and update the doc row with the measured breakdown. Any
site that is not should be fixed.

**`bugprone-easily-swappable-parameters` (29: 20 in `src`/`tools`, 9 in
tests).** Doc: "a subjective refactor suggestion". The check has options:
`MinimumLength` (default 2), `IgnoredParameterNames`,
`SuppressParametersUsedTogether` (on by default), `QualifiersMix` and
`ModelImplicitConversions` (on by default). List the 29 sites. For each one,
decide whether a swapped call would compile and be wrong. Two `const char *`
paths (src and dst) are a real hazard. A `(buf, cap)` pair is not. If a small
option set, e.g. `MinimumLength: 3` plus a few ignored names, leaves only
the real hazards, fix those and enable the check narrowed. Otherwise close
`wontfix`.

## Acceptance criteria

- [ ] Each check is enabled (narrowed as needed) with the gate at exit 0, or `wontfix` with the per-site classification in the comments. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] Each doc row is removed, or rewritten with the measured breakdown in place of "fires on every" and "subjective". `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
