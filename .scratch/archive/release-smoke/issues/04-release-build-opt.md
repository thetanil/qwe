# 04: The release build is -c opt

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

Releases are built at Bazel's default `fastbuild` (`-O0`). Add a `release` config in `.bazelrc`:
`--compilation_mode=opt` plus `--copt=-g`, so `qwe-debug` keeps DWARF. The strip rule still strips
`qwe`. `release.yml`'s build step uses `--config=release`.

Optimisation turns on warnings (`-Wmaybe-uninitialized`, `-Wstringop-*`) and `_FORTIFY_SOURCE` checks
that `-Werror` makes fatal. Fix them in our own code. Do not suppress them, and do not touch
`third_party/` (it builds with `-w`). There is no `assert()` under `src/`, so `NDEBUG` changes nothing;
confirm that is still true.

Update README "The binary" and "Getting it" (build command), and `docs/ci-checks.md`.

## Acceptance criteria

- [x] `bazel build --config=release //src/cli:qwe //src/cli:qwe-debug` succeeds with no warnings: `manual: run it locally, attach the tail of the output as a comment`
- [x] `bazel test --config=release //...` is green, ssh cases included where reachable: `manual: run it locally in the devcontainer, record the pass count as a comment` (CI enforces it from ticket 05)
- [x] `qwe` is stripped and static, and `qwe-debug` has a `.debug_info` section: `manual: file bazel-bin/src/cli/qwe; readelf -S bazel-bin/src/cli/qwe-debug | grep debug_info`
- [x] `release.yml` builds with `--config=release`: `unit: tools/ci/workflows_test.sh::repo_is_consistent` (the command is listed in docs/ci-checks.md)
- [x] `bazel test //...` green

## Comments

- `.bazelrc` gained `build:release --compilation_mode=opt` and `build:release --copt=-g`.
  `bazel build --config=release //...` (all 298 targets, third_party included) built clean —
  no `-Wmaybe-uninitialized`/`-Wstringop-*` findings surfaced anywhere.
- `_FORTIFY_SOURCE`'s `-Werror=unused-result` did fire, on two of our own test files that
  ignored `pipe`/`write`/`fread`'s return values: `src/kernel/preamble_test.c` (a test's own
  subprocess-pipe helper) and `src/kernel/summary_test.c` (its `slurp()` helper, added in
  ticket 03). Both now check and `abort()`/use the actual byte count on failure — first
  real evidence the release config catches something fastbuild doesn't.
- Confirmed `bazel build --config=release //src/cli:qwe //src/cli:qwe-debug`: `qwe` is
  `ELF ... statically linked ... stripped`; `qwe-debug` has a `.debug_info` section.
- `bazel test --config=release //...`: 224 passed, 3 skipped (sanitizer/valgrind smoke tests,
  unrelated to this config), 0 failed. ssh e2e cases ran and passed (`REMOTE_CONTAINERS` is set
  in this devcontainer).
- No `#include <assert.h>` and no C `assert()` anywhere under `src/` (grepped); the few
  `assert(...)` hits are Lua's own `assert` inside embedded Lua strings/scripts, unrelated to
  `NDEBUG`.
- `release.yml`'s build step now runs `bazel build --config=release //src/cli:qwe
  //src/cli:qwe-debug`; the same command is listed under `docs/ci-checks.md`'s "On demand"
  block (previously an empty/unclosed code fence — now closed and populated), which is what
  `tools/ci/workflows_test.sh::repo_is_consistent`'s rule 2 checks against.
- `bazel test //...` (plain fastbuild): 224 passed, 3 skipped, 0 failed. Coverage floor holds.
