# 09: Re-verify the two style exclusions

Status: wontfix
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

- [x] Each check is enabled (narrowed as needed) with the gate at exit 0, or `wontfix` with the per-site classification in the comments. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] Each doc row is removed, or rewritten with the measured breakdown in place of "fires on every" and "subjective". `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

Both stay excluded. Measured at `407f76a` by removing each exclusion in turn
(and restoring it): `.clang-tidy` is unchanged by this ticket, only its doc rows.

## `bugprone-multi-level-implicit-pointer-conversion`: 50, all the allocator idiom

Every finding has one of three messages:

| Message | Count | Callee |
|---|---|---|
| `T **` to `void *` | 35 | `free` 27, `qsort` 3, the argument of `realloc` 5 |
| `void *` to `T **` | 13 | result of `realloc` 5, `calloc` 5, `qwe_xcalloc` 2, `malloc` 1 |
| `const void *` to `const char *const *` | 2 | `key_cmp`'s `qsort` comparator in `luacbor.c`, once for `a` and once for `b` |

By file: `luaexec.c` 15, `luacbor.c` 9, `workflow.c` 7, `run.c` 7, `validate.c` 5,
`luafs.c` 5, `jobs.c` 2. No site is anything other than a `void *` through an
allocator, `free` or `qsort`, so none is a bug to fix. The check has no options,
so the only alternative to the exclusion is an explicit cast at each of the 50,
against the "do not cast `malloc`" idiom. Doc row rewritten with these numbers.

## `bugprone-easily-swappable-parameters`: 30, no option set separates the hazards

21 in `src/`+`tools/`, 9 in tests. Options tried:

- `ModelImplicitConversions: false`: drops 6 (5 in `src/`: `qwe_lc_event_is_stale`,
  `qwe_sched_pass`, `qwe_trace_record`, `job_event`, `ev_add`, mixes of `int`,
  `long` and an enum; 1 in a test, `run` in `preamble_test.c`). Those are noise:
  the arguments differ in meaning, not in type, and a C compiler would not
  catch a swap of two `long`s either way. 24 remain.
- `MinimumLength: 3` drops all 30 (no run is three long; the gate exits 0), so it
  is the exclusion under another name.
- `IgnoredParameterNames` can only match a name. The three `qsort` comparators
  (`cmp`, `key_cmp`, `cmp_problem`, all `(const void *a, const void *b)`) could go
  by naming `a` and `b`, but that would also silence `ssh_call`'s real `a`, `b`.

The 24 that no option removes:

- **3 `qsort` comparators.** Their signature is fixed by `qsort`, and only
  `qsort` calls them. Not a hazard.
- **8 test helpers:** `write_file(name, body)` (four files), `has_file`,
  `preamble_test`'s `run`, `spawn_with_fd_room` and `stat_group_session` (two
  out-parameters each). A swapped call writes the wrong file or reports the
  wrong number, and the test that calls it fails at once. Not a hazard.
- **13 findings in 12 functions in `src/` and `tools/`:** `qwe_positions_add`
  (key, value), `check_props` (anchor, tag), `qwe_xstrdup_at` (`s`, `file`; only
  ever reached through the `qwe_xstrdup` macro, which passes `__FILE__`),
  `convert_container` (depth, is_map), `qwe_oom_probe` (n, child_n),
  `qwe_preamble_build` (names/values, and two `size_t *` out-parameters),
  `group_used` (n, group), `qwe_summary_render` and `qwe_summary_write`
  (run_dir, workflow_file), `load_inventory` (cmd, wf_path), `send_result`
  (idx, fd) and `ssh_call` (fn, a, b). A swap of any compiles and is wrong. Each
  has a handful of call sites in the same or a neighbouring file, and each of
  those pairs differs in meaning enough to fail an obvious run, but that is a
  judgment, not a proof. The fix would be a struct of named arguments per
  function, or a distinct type per argument, which is a refactor of 12 interfaces
  for a class of mistake this codebase has not made (no fix in the log of
  `sca-findings` or here came from it).

So no small option set leaves only the real hazards: the options either remove
the check (`MinimumLength: 3`) or leave 21 findings that are mostly benign. The
doc row is rewritten with these numbers in place of "subjective".

`bazel test //...` 260 pass, 3 skipped; gate exit 0; coverage check green.
