# 04: The release build is -c opt

Status: ready-for-agent
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

- [ ] `bazel build --config=release //src/cli:qwe //src/cli:qwe-debug` succeeds with no warnings: `manual: run it locally, attach the tail of the output as a comment`
- [ ] `bazel test --config=release //...` is green, ssh cases included where reachable: `manual: run it locally in the devcontainer, record the pass count as a comment` (CI enforces it from ticket 05)
- [ ] `qwe` is stripped and static, and `qwe-debug` has a `.debug_info` section: `manual: file bazel-bin/src/cli/qwe; readelf -S bazel-bin/src/cli/qwe-debug | grep debug_info`
- [ ] `release.yml` builds with `--config=release`: `unit: tools/ci/workflows_test.sh::repo_is_consistent` (the command is listed in docs/ci-checks.md)
- [ ] `bazel test //...` green

## Comments
