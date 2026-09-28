# 05: Lint every compile configuration, and the fuzz harness

Status: resolved
Category: bug
Type: task

## What

`tools/clang-tidy/run.sh` builds its file list from `bazel aquery
'mnemonic("CppCompile", //src/... + //tools/...)'` and, for a file compiled
more than once, keeps only the first compile action ("A file built more than
once (rare) is checked once"). Measured at `bb9d69f`:

- `src/kernel/trace.c` is compiled twice. The first action carries
  `-DQWE_NO_STRERRORNAME_NP -Dstrerrorname_np=qwe_strerrorname_np_must_not_be_called`
  (`errno_name_test`'s variant), so the **fallback** table is linted and the
  production `strerrorname_np` branch is not.
- `src/kernel/errstr.c` and `tools/bcembed.c` are compiled three times each
  (target, exec and host configurations).
- `//src/edge/yaml:chain_fuzz` and `//src/edge/yaml:transcode_fuzz` are
  `manual`-tagged, so the wildcard skips them: the fuzz harness sources are never
  linted.

"Rare" is not an assessable answer: the code the gate skips is exactly the code
built with unusual flags.

## Fix

- Lint every distinct (file, flag set) pair, not every file. Report a finding by
  file:line and the configuration, and count a finding once across
  configurations.
- Include `manual`-tagged cc targets: run the query with the tags overridden
  (`bazel aquery` on the explicit targets, or a second query
  `attr(tags, manual, kind("cc_.* rule", ...))` unioned in), and say which
  targets are added.
- Lint the `.bazelrc` configurations that change what the preprocessor sees, not
  only the default one: at least `coverage` (`-DQWE_GCOV`, which compiles the
  `src/kernel/gcov.h` branch nobody has linted), `fuzz` (`--define=qwe_fuzz=1`),
  `valgrind` (`--define=qwe_valgrind=1`) and the sanitizer configs if any
  `select()` or `#ifdef` keys on them. Run the aquery once per configuration
  (`bazel aquery --config=<c>`, and `bazel aquery` under the `coverage` command's
  flags) and union the (file, flag set) pairs. List which configurations add
  nothing new and drop only those.
- The coverage configuration will report `__gcov_dump`/`__gcov_reset`
  (`gcov.h`) as reserved identifiers. Those names are fixed by libgcov; add them
  as exact names (`^__gcov_dump$;^__gcov_reset$`) to the root
  `AllowedIdentifiers` here, and ticket 15 moves them into a directory-scoped
  config. Every other new finding is fixed in code.
- If a configuration's flags make a file unparseable for clang (a GCC-only
  flag), fix the flag filter in `flags_for_file` rather than dropping the file.
- Print the list of (file, configuration) pairs it covered, and its count, so the
  evidence in ticket 04 shows coverage. A test compares the set of `.c` files
  under `src/`, `tools/` and `plugins/` (excluding `third_party/`) with the
  set covered and fails on any file that is in neither the list nor an explicit,
  reasoned allow-list. The allow-list should be empty.

## Acceptance criteria

- [x] `trace.c` is linted with and without `QWE_NO_STRERRORNAME_NP`.
      `manual: run.sh's coverage list names both`
- [x] The two fuzz binaries' sources are linted. `manual: coverage list`
- [x] `gcov.h`'s `QWE_GCOV` branch is linted (the coverage configuration is in
      the list), and so is every other configuration that adds a `-D` or a
      `select()` branch. `manual: coverage list; a throwaway atoi inside
      #ifdef QWE_GCOV fails the gate`
- [x] A throwaway `atoi` in `trace.c`'s `#else` (production) branch fails the
      gate; so does one in its fallback branch. `manual: add each, see exit
      non-zero, revert`
- [x] Every `.c` under `src/`, `tools/` and `plugins/` is covered, checked by a
      test. `unit: tools/clang-tidy/coverage_test.sh`
- [x] Any finding the newly covered code produces is fixed in code (Comments list
      them). `manual: gate at exit 0`
- [x] `docs/static-analysis.md`'s "Scope" no longer says a manual target "is
      never linted" or that a file is "checked once".
- [x] `bazel test //...` is green.

## Comments

Resolved. What was measured and done:

- **Pairs.** `run.sh` now lints every distinct (file, flag set), 186 at `995b7b3`
  (`run.sh --list` prints them and the count; the gate prints the same list, and the
  evidence bundle's `files.txt` holds it). It enumerates `default`, `coverage` (read
  from `.bazelrc`'s `coverage` lines), `fuzz`, `valgrind`, `ubsan` and `asan`. Two
  compiles that differ only in the `bazel-out/<configuration>/` directory name are one
  pair; the exec-configuration compiles of `alloc.c`, `errstr.c` and `bcembed.c` are
  not, they carry `-D_FORTIFY_SOURCE=1 -DNDEBUG`.
- **Configurations that add nothing new.** `fuzz` adds no pair the default does not
  have, and `release` adds only `-g`. `release` is dropped; `fuzz` is kept and
  enumerated, so a future `select()` on `qwe_fuzz` is linted (the aquery costs well
  under a second). `valgrind` adds `valgrind_smoke_test.c`; `ubsan` and `asan` each
  add one `-DQWE_SMOKE_*` build of `sanitizer_smoke_test.c`; `coverage` adds
  `-DQWE_GCOV` to 90 compiles.
- **Manual targets added:** `//src/edge/yaml:chain_fuzz`, `//src/edge/yaml:transcode_fuzz`
  (printed by `run.sh`). `fuzz_harness.c`, `chain_fuzz.c` and `transcode_fuzz.c` are linted.
- **Findings the new coverage produced (3), all handled:**
  - `__gcov_dump`, `__gcov_reset` in `gcov.h` (`bugprone-reserved-identifier`,
    `cert-dcl37-c`, coverage only): added as exact names to both `AllowedIdentifiers`,
    as the ticket says; ticket 15 moves them.
  - `valgrind_smoke_test.c:21`, `clang-analyzer-core.UndefinedBinaryOperatorResult`
    (valgrind only): the deliberate uninitialised read the test exists to commit.
    A `volatile` pointer and an indirect `malloc` were both tried and do not hide it
    (the analyzer traces the value to `malloc`), so it is one `NOLINTNEXTLINE` with the
    reason. This is the one finding not "fixed in code" in the sense of a change to the
    flagged logic: it cannot be, the fault is the point. The test still passes under
    `--config=valgrind`.
- **Mutation checks** (each applied, gate run, reverted): a throwaway `atoi` fails the
  gate (exit 1, `cert-err34-c`) in `trace.c`'s production `#else` branch, reported under
  the default pair and the `coverage` one; in its fallback table, reported under
  `-DQWE_NO_STRERRORNAME_NP`; and in `gcov.h` under `#ifdef QWE_GCOV`, reported under
  `coverage` for eight files.
- **Findings are reported once** by `file:line:col` and check, with the configurations
  they appeared under (`in: ...`), however many pairs reported them.
- **Test:** `tools/clang-tidy/coverage_test.sh` compares the 90 `.c` files under `src/`,
  `tools/` and `plugins/` with the list (allow-list empty), and pins `trace.c`'s two
  variants, the fuzz sources and `-DQWE_GCOV`. It is not a Bazel test (it needs
  `bazel aquery`, which cannot run in a sandbox); `static-analysis.yml` runs it before the gate.
- **Cost:** the gate went from about a minute to about two (93 to 186 runs).
- **Docs:** "Scope" rewritten, no longer says a manual target "is never linted" or that a
  file is "checked once"; the evidence, reserved-identifier, timing and `ci-checks.md`
  rows updated.
- `gate at exit 0` and `bazel test //...` (263 passed, 3 skipped) both hold.

Copilot review of PR #7, both points taken:

- A failed `bazel query` for the manual targets was swallowed: `bazel_out` ran inside
  `mapfile < <(...)`, whose subshell discards its `exit 1`, so `run.sh` went on with an
  empty manual list and silently dropped the fuzz targets. Reproduced with a `bazel`
  shim that fails only `query` (exit 0, 182 pairs instead of 186); the query results now
  come through command substitution, so the failure stops the script (exit 1, no list).
  The `third_party` genrule query had the same shape and got the same fix.
- The count line counted rows, and a row is one flag set with the configurations that
  share it, not one (file, configuration). It now reads `186 (file, flag set) pairs`
  (`run.sh`, `coverage_test.sh`, the docs).
