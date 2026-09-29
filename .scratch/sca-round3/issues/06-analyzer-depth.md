# 06: The clang analyzer, deeper

Status: resolved
Category: enhancement
Type: task
Blocked by: 04

## What

The doc calls `clang-analyzer-*` "symbolic execution, cross-function". It is
cross-function only inside one translation unit. Three ways to make it look
harder, each a measured experiment, each adopted only if it finds something
real; every new finding is fixed in code (the premise of this round).

1. **Cross-translation-unit analysis.** Without it, a function defined in
   another `.c` is a black box: a `NULL` returned from `qwe_x*`, a buffer freed
   by a callee in another file, an fd closed elsewhere are all invisible.
   `clang-extdef-mapping-20` and `analyze-build` are installed. CTU needs an
   extern-definition map and per-file AST dumps; both can be produced from the
   `bazel aquery` flags `run.sh` already extracts, with no Python (`analyze-build`
   is a Python script: do not use it, produce the map with `clang-extdef-mapping`
   directly and pass `-analyzer-config experimental-enable-naive-ctu-analysis=true`
   and `ctu-dir=`). If it cannot be done without Python or without a fragile
   layout, report that in the Comments and stop this part.
2. **POSIX modelling.** `unix.StdCLibraryFunctions:ModelPOSIX` teaches the
   analyzer the return conventions of `open`, `read`, `write`, `close`, `pipe`,
   `dup2`, `fcntl`. qwe is a fork/exec/pipe program. Check the default first:
   it is believed to have become `true` in clang 19, in which case clang-tidy 20
   already has it on and this part is a no-op to record, not a setting to add
   (`clang-20 -cc1 -analyzer-config-help` or a small test file with a known
   `read` misuse settles it). If it is off, try it via `-Xclang -analyzer-config
   -Xclang unix.StdCLibraryFunctions:ModelPOSIX=true` in `run.sh`'s extra args.
3. **Path-sensitivity settings**: `-analyzer-config` `max-nodes`,
   `max-inlinable-size`, `ipa=dynamic-bifurcate`, `crosscheck-with-z3=true` (only
   if the installed clang was built with Z3; if not, say so). Look at what
   each changes on this tree, not at what the docs promise.

The analyzer's alpha checkers are not exposed by clang-tidy (`--list-checks
--checks='clang-analyzer-*'` lists 286, none `alpha`); running the real
`scan-build`/`clang --analyze` with alpha checkers is out of scope for this
feature.

## Acceptance criteria

- [x] For each of the three, the number of new findings on this tree, the ones
      that are real bugs, the ones that are not, and a decision to adopt it,
      are in the Comments. `manual: Comments`
- [x] Each adopted setting is in `run.sh` or `.clang-tidy` with a comment naming
      the ticket, and the gate is green with it. `manual: run.sh at exit 0`
- [x] Each adopted setting makes the analyzer see more, not less, and is
      recorded as a `widen` row for ticket 28's `tools/ci/exceptions.txt` (list
      them in the Comments; ticket 28 creates the file). A setting that lowers a
      limit to save time (`max-nodes` down) is a `narrow` and is not adopted.
      `manual: Comments`
- [x] Every new real finding is fixed, with a test where it is an error path.
- [x] A finding that is a false positive is removed by restructuring the code
      so the analyzer sees the invariant (an early return, a local, an `assert`
      the code keeps), not by a `NOLINT`. If that costs more than a `NOLINT`
      would, say so in the Comments and stop before adopting that setting: an
      analysis mode you must suppress is not worth adopting.
- [x] `docs/static-analysis.md` says what the analyzer sees (per translation unit
      or across them) and which settings are on. `manual: doc`
- [x] `bazel test //...` is green.

## Comments

Worked on `sca-round3/06-analyzer-depth`, from `origin/main` at `6230c8a`. Every count
below is `tools/clang-tidy/run.sh`'s gate over the 186 (file, flag set) pairs, clang-tidy
20.1.8. Baseline: exit 0, 1 m 58 s.

### 1. Cross-translation-unit analysis: adopted

- **How, without Python or a fragile layout:** on-demand CTU. `clang-extdef-mapping` is run
  once per file (one entry per file, its default-configuration flags from the `aquery` data
  `run.sh` already has) to build `externalDefMap.txt`; `jq` writes `invocations.yml` (JSON,
  which YAML accepts) with each file's flags. The analyzer parses an imported file itself,
  so no AST dumps and no `clang` binary are needed: only `clang-extdef-mapping` from the same
  pinned tarball, which `.github/actions/clang-tidy-pin` now extracts too (and checks its
  version). USRs defined in more than one file (every `main`, greatest, the `__wrap_*`
  shims, `LLVMFuzzerTestOneInput`: 30) are dropped; 140 definitions remain.
- **It has to go in `-Xclang -analyzer-config`, not `CheckOptions`.** On a two-file toy (a
  callee in `b.c` frees what `a.c` then reads), `clang-analyzer-experimental-enable-naive-ctu-analysis`
  and friends as `CheckOptions` reported nothing; the same options as `-Xclang
  -analyzer-config` in the compile args reported the use-after-free. (A null-return toy
  shows nothing either way: the analyzer suppresses a null returned by an inlined callee,
  even inside one file.) Ticket 28 checks `CheckOptions` keys; this setting lives in
  `run.sh`'s `ctu_args`, which the test has to read too.
- **Silent failure guarded:** CTU that cannot match its map imports nothing and reports
  nothing. `display-ctu-progress=true` prints one `CTU loaded AST file:` per import and
  `run.sh` fails if the count is 0. Checked: with a stand-in `clang-extdef-mapping` that
  prints nothing, the gate exits 1 (`exit_status.txt` 1) with "imported no file in any
  run". `ctu-import-threshold` is set to the number of files (default 24; `workflow.c`
  already imports 20).
- **New findings: 15.** 11 real, 4 false positives, all fixed in code, no `NOLINT`:
  - real: `child_argv` with an empty plugin argv ran `execvp(NULL, argv)` in the child
    (`proc.c:66` via `workflow.c`; `core.NonNullParamChecker`). Test:
    `tests/e2e/plugin_argv_empty` (red before: `exit-code`, empty log; green after:
    `plugin-error`, "the plugin returned an empty argv"). Next to it, not an analyzer
    finding (`lua_tostring` is LuaJIT's, outside the map): a non-string element went
    through `strdup(lua_tostring(...))` = `strdup(NULL)`. Fixed with it, test
    `tests/e2e/plugin_argv_not_a_string` (red before, green after).
  - real: 9 leaks on a failed assert in tests, memory allocated in another file and freed
    after an `ASSERT` (`alloc_test.c` 88, 90; `redact_test.c` 32, 54, 80, 110, 123;
    `envelope_test.c` 31, 32; `unix.Malloc`). Now `qwe_own()`, or compared, wiped and
    freed before the asserts (`envelope_test`'s plaintext).
  - real: `secrets/oom_test.c`'s `open_case` (and `seal_case`, same shape) leaked its
    result on success. Freed.
  - false positive: `redact.c:127` (`realloc` of 0) and `redact.c:134` (`memcpy` to NULL):
    both need `total - i == 0` inside `while (i < total)`. The loop now counts `rest`
    down, and `rest > 0` is its condition.
  - false positive: `jobs.c:186` NULL `jobs` with `n > 0` (`select_jobs` not inlined, so
    `n` came back unknown). `qwe_jobs_free` returns early on NULL.
  - false positive: `transcode.c:420` `TaintedAlloc`: `len <= QWE_YAML_MAX_SIZE` is
    checked, but the solver does not carry that through `len * 2 + 64`. The doubling loop
    now has a ceiling on `cap` (64 × `QWE_YAML_MAX_SIZE`; no aliases, so CBOR is a small
    multiple of the YAML), which the analyzer sees and which the loop lacked. Not tested:
    no document can reach it.
  None cost more than a `NOLINT` would have.
- **The imported file's driver path matters.** Run with only the pinned tarball's tools (as
  CI has them), the first version failed: 234 imports instead of 314, and "'stddef.h' file not found" in the rest, because the
  invocation list's `argv[0]` was a bare `clang` and builtin headers are found relative to
  it. The devcontainer's `/usr/bin/clang` had hidden that. `argv[0]` is now the real path
  of the clang-tidy being run; with the pinned `clang-tidy` + `clang-extdef-mapping` the
  gate is at exit 0, 314 imports, no missing header, and the same with the apt ones.
- **Cost:** 1 m 58 s to 2 m 30 s. 314 imports at this commit.
- **Decision: adopted** (`run.sh`, comment naming this ticket; the gate is at exit 0 with it).

### 2. POSIX modelling: already on, nothing to add

`clang-20 -cc1 -analyzer-checker-option-help` does not list `ModelPOSIX` (it is a
developer option), so a test file settled it: `open()` unchecked, then `dup2(fd, 0)`.
clang-tidy 20 reports `The 1st argument to 'dup2' is -1 but should be >= 0
[clang-analyzer-unix.StdCLibraryFunctions]` with the default and with
`unix.StdCLibraryFunctions:ModelPOSIX: true`, and nothing with `false` (same with
`clang-20 --analyze`). So it is on by default: 0 new findings, no setting added. The
`dup2` finding in the doc's "first run" list was this model already.

### 3. Path-sensitivity settings: none adopted

- `mode` is `deep` by default, so `ipa=dynamic-bifurcate` is already the default (setting it
  explicitly: 0 findings, 1 m 53 s, no change). `max-inlinable-size` is 100, `max-nodes`
  225,000.
- `max-nodes=2000000`: without CTU 0 new findings, 4 m 56 s (2.5×). With CTU: 1 new finding, 7 m 12 s
  (partly alongside `bazel test //...`, so an upper bound; 2.5× without CTU). It is a false positive: `workflow.c:1926` dereferences a NULL `jobs` with
  `n > 0`, on the path where a workflow has no jobs and `select_jobs(jobs, &n, opts)` is
  not inlined, so `n` comes back unknown. `select_jobs` only shrinks `n`. The same class
  as `jobs.c:186` above, which the default limit already reached. Not fixed: the setting
  is not adopted, and the only fix is a guard in the caller for an invariant the callee
  already keeps.
- `max-inlinable-size=1000`: without CTU 0 new findings, 1 m 49 s. With CTU: 0 new findings, 2 m 26 s.
- `crosscheck-with-z3=true`: not available. Neither the pinned LLVM 20.1.8 release (`ldd`:
  libm/libz/libc only, no Z3 symbols) nor Ubuntu's `clang-tidy-20` is built with Z3; both
  abort with `LLVM ERROR: LLVM was not compiled with Z3 support`.
- **Decision:** none adopted. A higher limit that finds nothing only costs every PR time;
  it can be measured again as the code grows. Lowering any of them is a `narrow` and was not
  considered.

### `widen` rows for ticket 28's `tools/ci/exceptions.txt`

In `tools/clang-tidy/run.sh` (`ctu_args`, passed to the gate and to `--raw`), not
`.clang-tidy`:

- `experimental-enable-naive-ctu-analysis=true` (with `ctu-dir`, `ctu-invocation-list`):
  widen, ticket 06. Calls into other files of the scope are inlined instead of opaque.
- `ctu-import-threshold=<number of files>`: widen, ticket 06. Raised from 24 so no file's
  imports are cut short.
- `display-ctu-progress=true`: output only (the import count the gate checks); changes
  nothing the analyzer reports.

### Other

- Docs: `docs/static-analysis.md` has "What the analyzer sees" (per TU → across TUs, and
  every setting's state) and "What cross-translation-unit analysis found"; the intro,
  evidence (`ctu-map.txt`) and timing lines, and `docs/ci-checks.md`'s row are updated.
- Why no sanitizer caught the real ones: the test leaks only happen when an assert fails,
  the `oom_test` leak is in a probe child that ends with `_exit` (LeakSanitizer runs at
  `exit`), and no test had a plugin return an empty or non-string argv.
- `bazel test //...`: 265 passed, 3 skipped.
