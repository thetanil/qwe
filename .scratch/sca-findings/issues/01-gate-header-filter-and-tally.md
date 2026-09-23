# 01: Fix the gate's header filter and make the excluded backlog visible

Status: ready-for-agent
Category: bug
Type: task

## What

Two problems with the gate itself, to fix before any check is re-enabled.

**`HeaderFilterRegex: '^(src|tools)/.*'` never matches.** clang-tidy matches
the filter against the header's resolved path, and for the `-iquote .` style
includes Bazel passes, that path is absolute. Reproduced with a two-file
tree: a header containing `atoi(s)` gives 0 `cert-err34-c` findings under
the anchored regex and 1 under `.*`. No header has a finding today (a `.*` run
gives the same 619 findings), so nothing is hidden right now. The first real
finding in a `src/*.h` or `tools/*.h` header would be hidden, though.

Beware: `'/(src|tools)/'` is not the fix. `third_party/luajit/src/*.h`
matches it. clang-tidy 20 has `ExcludeHeaderFilterRegex`, so
`HeaderFilterRegex: '.*'` plus an exclude for `third_party/`,
`bazel-out/` and `external/` is one option. Prove the filter works with a
throwaway finding in a `src/` header, then remove it.

**The excluded backlog is invisible.** `--quiet` prints only "N warnings
generated", about 1,000 of them, with no hint that each is an excluded check
or a system-header finding. Add a mode (e.g. `tools/clang-tidy/run.sh --raw`)
that runs the raw ruleset with no exclusions and no test narrowing, and prints a
per-check tally split into `src/`+`tools/` and `*_test.c`. The run
behind `spec.md`'s tables did exactly this. Tickets 02 to 07 and the next
feature use it to measure progress. The default mode stays a pass/fail gate.

## Acceptance criteria

- [ ] A finding in a `src/` header fails the gate, and a finding in `third_party/` does not. `manual: add a throwaway static inline int f(const char *s){ return atoi(s); } to a src/kernel/*.h header included by a .c file and temporarily enable cert-err34-c; CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh must exit non-zero naming that header; revert`
- [ ] `run.sh` still exits 0 on the tree. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] `run.sh --raw` prints the per-check tally, and its total matches `spec.md` (619 at `acb416d`, or the count after any intervening fixes, with the difference explained). `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh --raw`
- [ ] `docs/static-analysis.md` documents the header filter and `--raw`. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
