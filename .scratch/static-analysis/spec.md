# Static analysis gate

Status: resolved

## What

Add an LLVM static-analysis gate (the Clang Static Analyzer, via `clang-tidy`,
plus a popular bugprone/cert/concurrency/performance/portability ruleset for
C) alongside the valgrind gate: same workflow file, same cadence (by hand,
nightly, in a release; never on a push), since both are the slow, thorough
checks that read the whole tree rather than the fast ones that run on every
push.

This is a different class of check from the sanitizers and valgrind: those
need a run that exercises the bad path; this reads every path without running
anything, at the cost of false positives a runtime check never produces. It
complements them rather than replaces any of them.

## Constraints

- No Python, anywhere, ever (`CLAUDE.md`). Every off-the-shelf way to feed
  Bazel-built compiler flags to `clang-tidy` goes through a `compile_commands.json`
  generator, and the standard one (`hedron_compile_commands`) is a `py_binary`
  under the hood. Confirmed by trying it: `bazel run @hedron_compile_commands//:refresh_compile_commands`
  prints a `#!/usr/bin/env python3` script and runs it.
- Scope matches the sanitizers (`docs/sanitizers.md`): `src/`, `plugins/` and
  `tools/` in full; `third_party/` is vendored and excluded.
- A finding fails the gate (`WarningsAsErrors: '*'`), the same rule as the
  sanitizers and valgrind: no recovery, no suppressed-and-ignored backlog.

## Approach

`bazel aquery 'mnemonic("CppCompile", //src/... + //tools/...)'` gives every
compile action Bazel actually runs, in JSON, including the exact flags and
generated-header paths — no Python needed, and it automatically excludes
`third_party/` and any `manual`-tagged target (the libFuzzer binaries), the
same rule that keeps them out of `bazel build //...`. `tools/clang-tidy/run.sh`
extracts, per file, the flags `clang-tidy`'s parser needs
(`-iquote`/`-isystem`/`-D`/`-std`) and drops the rest (GCC-toolchain plumbing
that means nothing to clang and can trip its driver).

`.clang-tidy` at the repo root is the ruleset. See `docs/static-analysis.md`
for the full checks list and the reasoning behind every exclusion from the
raw defaults.

## Tickets

- [01: the clang-tidy gate](issues/01-clang-tidy-gate.md) — resolved
