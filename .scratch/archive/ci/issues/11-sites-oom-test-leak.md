# 11: sites_oom_test leaks the redactor's buffers under valgrind

Status: resolved (locally; CI run pending)
Category: bug
Type: task
Blocked by: none

## What

In the first `valgrind.yml` run (19fb71c, run 35623889413), `//src/kernel:sites_oom_test`
failed under `--config=valgrind` in 2.3 s with definite leaks, all in
`redactor_survives_every_injection` (`sites_oom_test.c:59`, allocation 3 of 3):

- 4 bytes via `qwe_redact_feed` (`redact.c:127`), realloc
- 4096 bytes via `buf_add` (`redact.c:67`, from `qwe_redact_feed` at `redact.c:138`), realloc

Both are reached through `redact_case` (`sites_oom_test.c:23`) inside `qwe_oom_probe`
(`oom_shim.c:156`). The test's probe forks and fails allocation n. Decide whether this
is a real leak in the redactor's failure path (a buffer not freed when a later
allocation fails), or the probe child exiting without freeing what the test's own
`redact_case` owns. It passes under ASan and `bazel test //...` on the same commit, so
find out why valgrind sees it and LeakSanitizer does not, and whether it also occurs in
the devcontainer.

## Acceptance criteria

- [x] Cause found and written under Comments.
- [x] `bazel test --config=valgrind //src/kernel:sites_oom_test` passes. `manual: run in the devcontainer and in CI`
- [x] Not a redactor leak, so no new test: the fix is in the test itself.

## Comments

- Cause: the test's own helper, `redact_case`, returned without `qwe_redact_buf_free(&out)` or `qwe_redactor_free(&r)`. Not a leak in `redact.c`. Why ASan does not report it was not checked. It reproduced locally in the devcontainer, not only on the runner: the valgrind gate had not been run since `quality/08` added the test.
- Fix: `redact_case` frees both before it returns. `bazel test --config=valgrind //src/kernel:sites_oom_test` passes.
