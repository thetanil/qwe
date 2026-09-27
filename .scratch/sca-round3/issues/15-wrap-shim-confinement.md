# 15: Toolchain-mandated reserved names confined to two directories

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 05, 13, 14

## What

After tickets 13 and 14 the reserved-identifier allow list holds two patterns:
`__wrap_*` (20 sites) and `__real_*` (20 sites). These are not ours to rename:
`-Wl,--wrap=calloc` makes the linker resolve calls to `calloc` to `__wrap_calloc`
and `__real_calloc` to the real one. The OOM harness (`oom_shim`, and the tests
that define their own wrappers, e.g. `workflow_test.c`) depends on it, and it is
how libyaml's and libsodium's own `malloc`s are made to fail too (a hook in our
`qwe_x*` allocator would lose that).

There are two more the allow list does not name today, because the gate has
never linted the configuration they are compiled in (ticket 05):
`src/kernel/gcov.h` declares `extern void __gcov_dump(void)` and
`__gcov_reset(void)` under `#ifdef QWE_GCOV` (`bazel coverage` only). They are
libgcov's API; the names are fixed by GCC. Once ticket 05 lints the coverage
configuration they are findings.

So the names stay; what changes is where. Today they appear in `oom_shim.c`,
`gcov.h` and a handful of `*_test.c` files. The shipped product has none of the
`__wrap_*`/`__real_*` ones, but the allow list is global, so a `__wrap_x` in
shipped code would pass.

## Fix

- Move every `__wrap_*` definition and `__real_*` declaration into one
  directory, `src/testing/wrap/`, with `oom_shim.{c,h}` and one shared
  `wrap_calloc.c`-style file per wrapped function (`malloc`, `calloc`, `realloc`,
  `strdup`, `strndup`, `free`, `clock_gettime`), so a test that wants a different
  wrapper links a different file and does not define the name itself.
  The `workflow_test.c` wrapper of `calloc` ("fail only in the parent") becomes a
  mode of the shared shim.
- Move the two libgcov declarations into their own small header in their own
  directory, `src/kernel/gcov/` (`gcov.h` includes it under `QWE_GCOV`). It
  cannot go under `src/testing/`: `proc.c` calls `qwe_gcov_dump` in the product
  build's coverage configuration.
- Put a `.clang-tidy` in each of the two directories with
  `InheritParentConfig: true` and only that directory's names in
  `AllowedIdentifiers` (`src/testing/wrap/`: `^__wrap_.*$;^__real_.*$`;
  `src/kernel/gcov/`: `^__gcov_dump$;^__gcov_reset$`, exact names, no pattern),
  for both `bugprone-reserved-identifier` and `cert-dcl37-c`. Remove
  `AllowedIdentifiers` from the root `.clang-tidy` entirely. clang-tidy applies
  the nearest config to each file, so each allowance covers its directory and
  nothing else. (The gate mode of `run.sh` uses the default config lookup and
  honours it; `--raw` passes `--config-file` and must be told to include the
  nested files, ticket 28's concern.)
- Prototypes for `__wrap_*` live in `oom_shim.h` (ticket 08 asks for them).
- `//src/testing/wrap` is `testonly = True` with visibility limited to test
  targets, so a shipped target cannot depend on it (Bazel already enforces
  `testonly`).

The reserved names then exist, as an exception, in two directories: one of
test-only code that is not part of the certified binary, one of two
declarations that exist only in the coverage build. That is the whole of this
family's remaining deviation: one sentence each in `docs/static-analysis.md`,
two nested `.clang-tidy` files an assessor can read in ten lines, and a rule that
they can only ever apply there.

## Acceptance criteria

- [ ] `grep -rn "__wrap_\|__real_" src tools` finds only `src/testing/wrap/` and
      BUILD `linkopts`; `grep -rn "__gcov_" src tools` finds only
      `src/kernel/gcov/`. `manual: grep`
- [ ] Root `.clang-tidy` has no `AllowedIdentifiers`; the two nested
      `.clang-tidy` files have only their own names. The gate exits 0, including
      the coverage configuration once ticket 05 lints it. `manual: run.sh`
- [ ] A throwaway `__wrap_foo` in `src/kernel/clock.h` (or any shipped file)
      fails the gate, and so does a throwaway `__gcov_flush` declaration in
      `src/kernel/gcov/`. `manual: add each, see bugprone-reserved-identifier
      named, revert`
- [ ] The OOM harness tests still inject the same failures: same `oom_test`,
      `load_oom_test`, `sites_oom_test`, validate and encrypt oom tests, unchanged in
      count and outcome. `unit: the existing oom tests`
- [ ] `bazel coverage` still records forked children's lines. `manual: coverage check`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
