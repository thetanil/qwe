# 05: Lint every compile configuration, and the fuzz harness

Status: ready-for-agent
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

- [ ] `trace.c` is linted with and without `QWE_NO_STRERRORNAME_NP`.
      `manual: run.sh's coverage list names both`
- [ ] The two fuzz binaries' sources are linted. `manual: coverage list`
- [ ] `gcov.h`'s `QWE_GCOV` branch is linted (the coverage configuration is in
      the list), and so is every other configuration that adds a `-D` or a
      `select()` branch. `manual: coverage list; a throwaway atoi inside
      #ifdef QWE_GCOV fails the gate`
- [ ] A throwaway `atoi` in `trace.c`'s `#else` (production) branch fails the
      gate; so does one in its fallback branch. `manual: add each, see exit
      non-zero, revert`
- [ ] Every `.c` under `src/`, `tools/` and `plugins/` is covered, checked by a
      test. `unit: tools/clang-tidy/coverage_test.sh`
- [ ] Any finding the newly covered code produces is fixed in code (Comments list
      them). `manual: gate at exit 0`
- [ ] `docs/static-analysis.md`'s "Scope" no longer says a manual target "is
      never linted" or that a file is "checked once".
- [ ] `bazel test //...` is green.

## Comments
