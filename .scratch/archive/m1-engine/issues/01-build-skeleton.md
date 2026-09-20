# 01: Bazel skeleton, CLI dispatch, greatest

Status: resolved
Type: task
Blocked by: none

## What

Set up the repo so that `bazel build //...` and `bazel test //...` work under Bazel 8.7.0, using the layout agreed in the spec:
- `src/kernel/` (a library with no `main`)
- `src/edge/yaml/`, `src/secrets/`
- `src/cli/main.c` with a subcommand table, and `src/cli/{run,validate,encrypt,keygen,serve}/` as stub packages
- `plugins/builtin/`, `third_party/`, `tests/e2e/`

Vendor **greatest** into `third_party/greatest/`. Add a `.bazelrc` containing the `--test_env` lines from the spec. Add an e2e harness: a shell `sh_test` macro that runs the built `qwe` binary on a case directory and compares its output with golden files. Later tickets add cases to it.

## Acceptance criteria

- [x] `bazel test //...` passes on a clean checkout. `unit: src/cli/dispatch_test.c::known_subcommands_resolve`
- [x] An unknown subcommand prints usage to stderr and exits 2. `e2e: tests/e2e/cli_unknown_subcommand/`
- [x] `qwe --version` prints a version string and exits 0. `e2e: tests/e2e/cli_version/`
- [x] `qwe serve` prints "not implemented" and exits 2. `e2e: tests/e2e/cli_serve_stub/`
- [x] The e2e macro fails when the golden file differs from the output. `e2e: tests/e2e/harness_selftest_mismatch/` (a case expected to fail, run under a wrapper that inverts the result)
- [x] `src/kernel` has no `main` symbol and every subcommand links it through one public header. `unit: src/kernel/api_test.c::links_without_cli`

## Comments

Done. `bazel test //...` passes (6 tests).
- e2e tests are named `<case>_test`, not `<case>`. A test named after its case directory makes the test binary shadow the case directory in runfiles.
- Stubs (`run`, `validate`, `encrypt`, `keygen`, `serve`) print `qwe <cmd>: not implemented` to stderr and exit 2.
- e2e case layout: `args` (one per line), `expected/{stdout,stderr,exit}`, plus any other `expected/<path>` diffed against `<path>` in the scratch work dir. The runner exits 0 on pass, 1 on golden mismatch, 3 on harness error. The selftest wrapper passes only on exit 1.
