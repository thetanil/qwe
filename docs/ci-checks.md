# CI checks

There is no CI service configured yet. This is the list of checks the quality
feature built, with the exact commands, for the CI feature to wire up. Run
everything from the repository root; Bazel 8.7.0; clang for the fuzzers.

| Check | Command | Cadence | Cost | Fails when |
|---|---|---|---|---|
| Tests | `bazel test //...` | every change | seconds, warm cache | any test fails |
| ASan + LSan | `bazel test --config=asan //...` | every change | about the plain suite | a memory error or leak |
| UBSan | `bazel test --config=ubsan //...` | nightly | about the plain suite | undefined behaviour |
| Valgrind, unit tests | `bazel test --config=valgrind //...` | nightly | about 130 s (`load_oom_test` is most of it) | any error, leak or unsuppressed report |
| Valgrind, e2e | `bazel test //tests/e2e:valgrind_e2e` | nightly | about 6 s | the same, in qwe and its step children |
| Coverage floor | `bazel run //tools/luacov:check` | every change | about 35 s warm | any file under `src/` or `plugins/` has more uncovered lines than `tools/luacov/floor.txt` |
| Fuzzing | `tools/fuzz/nightly.sh [seconds]` | nightly | one hour by default, four processes in parallel | any crash artifact exists |

Details for each live in `docs/sanitizers.md`, `docs/valgrind.md`,
`docs/coverage.md` and `docs/fuzzing.md`. What follows is only what CI needs.

## Every change

```
bazel test //...
bazel test --config=asan //...
bazel run //tools/luacov:check
```

`check` runs `bazel coverage //... --combined_report=lcov` itself and compares
per-file miss counts with `tools/luacov/floor.txt`, a ratchet: new code without
tests raises a count and fails; deleting code cannot fail. After adding tests,
`bazel run //tools/luacov:check -- --update` rewrites the floor, and the result
is committed on purpose. For a browsable report, `bazel run //tools/luacov:html`
(needs `genhtml`, from the `lcov` package) writes `coverage-html/`; publish it as
a CI artifact.

## Nightly

```
bazel test --config=ubsan //...
bazel test --config=valgrind //...
bazel test //tests/e2e:valgrind_e2e
tools/fuzz/nightly.sh
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

## Not in CI yet

- `--config=valgrind` needs `valgrind` installed (3.22 was measured).
- Ticket `quality/09` (remaining C coverage gaps) is open; it adds tests to the suites
  above rather than a new command.

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
  uncovered in `tools/luacov/floor.txt` for the same reason; new injection code can
  raise those counts, and `--update` is the right response when that is all it is.
