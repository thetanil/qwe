# 02: run.sh fails on a warm disk cache with a fresh output base

Status: in-progress (PR open; the last criterion needs its CI run)
Category: bug
Type: task

## What

`tools/clang-tidy/run.sh` builds `//src/... //tools/...` "to materialize every
generated header" before it reads the compile actions. That does not cover
`third_party/luajit`'s `luajit.h` (a `genrule` outside `//src/...` and `//tools/...`).
With a warm disk cache and a fresh output base, Bazel serves every compile action
from the cache and never writes a header nobody asked for, so clang-tidy fails on the
one file that includes it: `src/edge/yaml/fuzz_harness.c:11:10: error: 'luajit.h' file
not found`.

Found on PR #4 (docs only): [run 36385080084](https://github.com/thetanil/qwe/actions/runs/36385080084),
where `static-analysis` was the only red check, with `825 processes: 526 disk cache hit,
299 internal`. Every `pull_request` run restores the cache a `main` push saved
(`cache-save` is false on a PR), so this is the state a PR starts in. It passed on `main`'s
own pushes, whose cache state differed; the next `main` push is exposed the same way.

## Acceptance criteria

- [x] `run.sh` exits 0 with a warm disk cache and a fresh output base. `manual: a bazel wrapper that pins --output_base and --disk_cache; run.sh once cold (fills the cache), then again with a new output base. Before the fix the second run exits 1 with "'luajit.h' file not found"; after, exit 0`
- [x] `run.sh` builds every genrule under `//third_party/...`, not only `luajit.h`'s, and exits 1 with a message if the query finds none. `manual: tools/clang-tidy/run.sh, the mapfile and its empty check`
- [x] `docs/static-analysis.md` says why the `third_party` genrules are named. `manual: docs/static-analysis.md, "run.sh runs bazel build"`
- [ ] The PR's own `static-analysis` check is green: it starts from the same restored cache that failed PR #4. `manual: gh pr checks on this ticket's PR`
- [x] `bazel test //...` is green.

## Comments

- **Why no automated test.** The failure needs a cold output base against a warm cache:
  two full builds, about five minutes, too slow for `bazel test //...` and not
  reproducible inside Bazel's sandbox. The criteria name the reproduction instead, and the
  PR's own CI run is the standing check: it starts from the cache state that failed.
- **Reproduced through the real script, red then green.** Run 1 (cold, unfixed) exit 0.
  Run 2 (new output base, warm cache, unfixed) exit 1, `'luajit.h' file not found`. Run 3
  (new output base, warm cache, fixed) exit 0. Building only `//third_party/luajit:gen_luajit_h`
  also wrote the header; the whole-genrule query was chosen so a header added to
  `third_party` later does not bring the failure back.
- **Scope.** All eight genrules under `//third_party/...` are LuaJIT's. Nothing else in
  `third_party` generates a header the linted files include.
