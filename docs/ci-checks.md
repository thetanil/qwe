# CI checks

GitHub Actions on `thetanil/qwe` runs every check below. Each is its own workflow in
`.github/workflows/`, with its own README badge (a status badge is per workflow file).
Run everything from the repository root; Bazel 8.7.0; clang for the fuzzers. The
`//tools/ci:workflows_test` test fails when a command in "Every push" is in no workflow,
or a workflow has no badge. `.github/actions/setup` is the shared setup: the runner
(`ubuntu-24.04`), Bazel at `.bazelversion`, apt packages, a Bazel disk cache per config,
and an sshd on `172.18.0.1` for the ssh e2e cases (`QWE_E2E_REQUIRE_SSH=1` makes a
skipped one a failure).

| Check | Workflow | Command | Cadence | Cost | Fails when |
|---|---|---|---|---|---|
| Tests | `tests.yml` | `bazel test //...` | every push to main | seconds, warm cache | any test fails |
| ASan + LSan | `asan.yml` | `bazel test --config=asan //...` | every push to main | about the plain suite | a memory error or leak |
| UBSan | `ubsan.yml` | `bazel test --config=ubsan //...` | every push to main | about the plain suite | undefined behaviour |
| Valgrind, unit tests | `valgrind.yml` (job `unit`) | `bazel test --config=valgrind //...` | nightly, release and manual | about 8 min locally (the three oom sweeps: `oom_test` 486 s); 23 min on a runner | any error, leak or unsuppressed report |
| Valgrind, e2e | `valgrind.yml` (job `e2e`) | `bazel test --config=valgrind //tests/e2e:valgrind_e2e` | nightly, release and manual | about 6 s locally, 2 min on a runner | the same, in qwe and its step children |
| Coverage floor | `coverage.yml` | `bazel run //tools/coverage:check` | every push to main | about 35 s warm | any file under `src/` or `plugins/` has more uncovered lines than `tools/coverage/floor.txt` |
| Smoke | `smoke.yml` (job `debug-smoke`, then `build`, then `smoke`) | `./qwe run tests/smoke/smoke_run.yml --summary "$GITHUB_STEP_SUMMARY"` | every push to main | debug-smoke seconds, fastbuild; build ~ the plain suite under `--config=release`; smoke seconds on a fresh runner | debug-smoke: a real bug in a smoke workflow, caught on the fastbuild binary before `build` pays for `--config=release`. build/smoke: the release binary fails a real smoke workflow, is not statically linked, or the full suite fails under `--config=release` (`build` then keeps a gdb backtrace of any crashed test as the `smoke-release-backtraces` artifact) |
| Fuzzing | `fuzz.yml` | `tools/fuzz/nightly.sh [seconds]` | nightly (3600 s) and manual | hours; four processes in parallel | any crash artifact exists |

Details for each live in `docs/sanitizers.md`, `docs/valgrind.md`,
`docs/coverage.md` and `docs/fuzzing.md`. What follows is only what CI needs.

## Every push

```
bazel test //...
bazel test --config=asan //...
bazel test --config=ubsan //...
bazel run //tools/coverage:check
bazel test --config=release //...
./qwe run tests/smoke/smoke_run.yml --summary "$GITHUB_STEP_SUMMARY"
```

`check` runs `bazel coverage //... --combined_report=lcov` itself and compares
per-file miss counts with `tools/coverage/floor.txt`, a ratchet: new code without
tests raises a count and fails; deleting code cannot fail. After adding tests,
`bazel run //tools/coverage:check -- --update` rewrites the floor, and the result
is committed on purpose. For a browsable report, `bazel run //tools/coverage:html`
(needs `genhtml`, from the `lcov` package) writes `coverage-html/`; the coverage workflow uploads it as
an artifact. On a green push to main it is also deployed to GitHub Pages (`https://thetanil.com/qwe/`, Settings, Pages, source "GitHub Actions"), next to `coverage.json`, the percentage badge document that `tools/coverage/badge.sh` writes (line coverage of `src/` and `plugins/`; red under 70, yellow under 85, green above).

## On demand

Valgrind takes 23 minutes on a runner, so `valgrind.yml` runs only from `workflow_dispatch` and
from `nightly.yml` and `release.yml` (by `workflow_call`), never on a push. `workflows_test` checks these commands too.
`release.yml` builds the shipped binaries with the release config (same codegen as fastbuild,
debug info kept for `qwe-debug`; the strip rule still strips `qwe`).

```
bazel build --config=release //src/cli:qwe //src/cli:qwe-debug
```

## How CI reaches ssh

The 19 e2e cases that need ssh connect to `172.18.0.1`, the devcontainer's host, which is
hardcoded in their `inventory.yaml` files. `.github/actions/setup` with `ssh-target: "true"`
(tests, asan, ubsan, valgrind, coverage) makes a runner look like that host:

- **The address.** `172.18.0.1/32` is added to `lo`, so it is reachable and needs no network.
- **sshd and a key.** sshd runs on the runner; a fresh ed25519 key is authorised for the runner
  user and held by an `ssh-agent` at `~/.ssh/agent.sock` (the cases authenticate through
  `SSH_AUTH_SOCK`). `known_hosts` is filled with `ssh-keyscan`, since the cases run with
  `BatchMode=yes` and cannot answer a host-key prompt.
- **No passwordless sudo.** `ssh_become_denied` needs a target user that cannot `sudo`, as on zeta.
  The runner user has a `NOPASSWD` rule, so the setup removes whichever files in `/etc/sudoers.d`
  grant it, then fails the job if `sudo -n true` still works, locally or over ssh. This is the last
  thing the step does: **no later step of the job may use `sudo`.**
- **Environment.** `REMOTE_CONTAINERS=1` and `QWE_E2E_REQUIRE_SSH=1` are written to `$GITHUB_ENV`,
  and `SSH_AUTH_SOCK` likewise. `~/.bazelrc` belongs to `setup-bazel`, so nothing goes there. The
  repo `.bazelrc` has `test --test_env=<NAME>` for each, which forwards the value from the
  environment to the tests; there is no second copy that could win. With `QWE_E2E_REQUIRE_SSH=1`
  an unreachable target fails the case instead of skipping it.
- **The agent's life.** The agent is started with `RUNNER_TRACKING_ID=""` so the runner does not
  kill it when the step ends. Every job gets its own VM, so the two `valgrind.yml` jobs each start
  their own sshd and agent and nothing outlives the job.
- **Fallback.** None is coded: the address is in the case files, and there is no host override
  variable. If the loopback alias ever stops working on runners, the alternatives are a different
  local alias in both the action and the inventories, or running the ssh cases only where a real
  target exists.

## Nightly and release

`nightly.yml` runs at 02:17 UTC and on demand. It first deletes every `setup-bazel-*` cache (a saved
cache key is never rewritten, so the gates' caches go stale), then calls all five gate workflows:
tests, asan, ubsan, valgrind and coverage. They run cold and save fresh caches, which pushes to main
then restore. It also calls `fuzz.yml` for 3600 seconds (release does not). `workflows_test` fails if it stops calling one of the five gates or the fuzzer.

`release.yml` runs on a pushed tag `v*`. Write the release in the GitHub web UI (its notes, and the tag it
creates on publish), or push the tag yourself. All five gates run again at the tagged commit (not the
fuzzer); then `qwe` and `qwe-debug` are built, `tools/release/check_version.sh` checks that the tag,
`QWE_VERSION` in `src/kernel/qwe.h` and `qwe --version` agree, and the two binaries and `SHA256SUMS` are
attached to the release. The full commit hash is appended to the release notes (a release created by the
workflow is titled `<tag> (<short sha>)`). A release made in the web UI is public while the gates run; if a
gate or the version check fails, the workflow turns it back into a draft. A pre-release tag
(`v0.2.0-rc1`) is published as a pre-release. Bump `QWE_VERSION` first, or the version check fails.
bazel test --config=valgrind //...
bazel test --config=valgrind //tests/e2e:valgrind_e2e
```

MSan is not supported (`docs/sanitizers.md` says why); do not add a job for it.
The `sanitizer_smoke_*` tests each config builds fault on purpose and pass only
if the sanitizer stops them, so a config that silently lost its flags fails
loudly rather than passing green.

## Fuzzing

`tools/fuzz/nightly.sh` builds `//src/edge/yaml:transcode_fuzz` and
`//src/edge/yaml:chain_fuzz` under `--config=fuzz` with `--config=asan` and
`--config=ubsan`, then runs all four for `seconds` (default 3600) in parallel.
It needs clang (`--config=fuzz` sets `CC=clang`) and 4+ idle cores.

- **Persistent corpus.** `$QWE_FUZZ_DIR` (default `~/.cache/qwe-fuzz`) holds
  `<target>-<san>/` corpora, `<target>-<san>.log` and `<target>-<san>-crashes/`.
  The job must cache or mount that directory between runs, or every night starts
  from the seeds again. Seeds come from `tests/e2e/*/w.yaml`, `inventory.yaml` and
  `src/edge/yaml/corpus/`, and the dictionary is `src/edge/yaml/qwe.dict`.
- **Exit status.** Non-zero if any file exists under a `*-crashes/` directory.
  Upload `$QWE_FUZZ_DIR/*-crashes/` and the `.log` files as job artifacts.
- **A crash becomes a test.** Once understood and fixed, copy the file into
  `src/edge/yaml/corpus/`. `//src/edge/yaml:corpus_test` (in `bazel test //...`,
  and under asan and ubsan) replays that directory and every e2e workflow through
  both entry points, so the fix stays fixed. Clear the crash artifact afterwards.
- **Scope.** Only `src/` is instrumented; libyaml and LuaJIT are not fuzzed
  (open question 10 in `workflow-kernel-design.md`).
- **Not yet recorded.** The ticket's one-hour run (iterations, corpus size,
  coverage) was dropped in favour of CI. The first scheduled runs should record
  them here.

Fuzz binaries are `manual`-tagged, so `bazel test //...` and `bazel build //...`
never build them.

## Allocation checks

No separate command: all of these are ordinary tests in `bazel test //...`, and so
also run under the asan, ubsan and valgrind configs above.

- **OOM injection** (`quality/07`, `08`): `oom_test` in `src/kernel`, `src/cli/validate`,
  `src/secrets`, `src/cli/run` and `src/cli/encrypt`, plus `src/kernel:sites_oom_test` and
  `load_oom_test`. Each fails the nth allocation for every n and fails on a fault, a
  hang, or a silently short success. Under valgrind they are the slow part.
- **Bare-call audit**: `//src/kernel:alloc_audit_test` compares the count of bare
  `malloc`/`calloc`/`realloc`/`strdup`/`strndup` per file with `src/kernel/alloc_audit.txt`.
  It fails when a call appears or goes; read the new call against `src/kernel/alloc.h`,
  then update the list. `tools/bcembed.c` is exempt (build-time tool).
- **Which allocations a test fails**: run the test with
  `QWE_OOM_SITE_LOG=<file>` (`--test_env`, and `--copt=-g --strip=never`), then
  `addr2line -i -e <test binary> $(sed 's/^/0x/' <file>)`. Coverage cannot show this,
  because the injected run is a forked probe that never flushes it.
- **Coverage floor**: the shim's probe code and the forked step children count as
  uncovered in `tools/coverage/floor.txt` for the same reason; new injection code can
  raise those counts, and `--update` is the right response when that is all it is.
