# Static analysis gate

```
tools/clang-tidy/run.sh
```

The LLVM/Clang toolchain's own static analysis: `clang-analyzer-*` is the Clang
Static Analyzer itself (symbolic execution, cross-function), run through
`clang-tidy` alongside a popular bugprone/cert/concurrency/performance/portability
ruleset for C. It finds a different class of thing from the sanitizers and
valgrind: they need a run that exercises the bad path; this reads every path,
without running anything, at the cost of false positives a runtime check never has.
It runs in its own `static-analysis.yml`, on every push to main, every pull request,
nightly, release and by hand (see `docs/ci-checks.md`): unlike valgrind, the gate is
about two minutes (one clang-tidy run per (file, flag set) pair, 186 of them, see "Scope"),
so it gates a pull request rather than waiting for the next nightly run.

## The pinned version

`tools/clang-tidy/pin.env` is the one file everything below reads: `docs/ci-checks.md`,
`run.sh` and `.github/actions/clang-tidy-pin`. CI installs exactly `CLANG_TIDY_VERSION`
(`20.1.8`, what every finding count in this file was measured with), downloaded from
LLVM's own GitHub release tarball and checked against its published `CLANG_TIDY_SHA256`
before use — apt.llvm.org only ever carries the latest point release of a major, so an
`apt install clang-tidy-20` pin silently drifts forward as upstream ships new point
releases; a URL pinned by content hash cannot. `run.sh` itself only enforces
`CLANG_TIDY_MAJOR` (`20`) against whatever `$CLANG_TIDY` resolves to, so a devcontainer's
apt-installed `clang-tidy-20` (tracking Ubuntu's `llvm-toolchain-noble-20` packaging, which
does move across 20.x point releases) still works locally; it refuses anything from a
different major, naming the pin file. Running it with `CLANG_TIDY=clang-tidy-18` (or any
non-20 major) exits non-zero without touching Bazel.

## Evidence

`run.sh --evidence-dir DIR` runs the same gate and additionally writes, whether it passes
or fails: `version.txt` (`clang-tidy --version`), `.clang-tidy` (a copy of the config as
run), `files.txt` (the exact (file, configuration) list `bazel aquery` resolved, and its
count: see "Scope"), `sha.txt` (the git commit), `output.txt` (every run's full output,
not `--quiet`, each behind a `@@RUN` line naming its pair) and `exit_status.txt`. CI uploads
this as the `clang-tidy-evidence-<sha>` artifact on every run of `static-analysis.yml`.
GitHub keeps a workflow artifact for at most 90 days (up to 400 on a private repo), so
`release.yml` also downloads it and attaches it to the GitHub release as
`clang-tidy-evidence-<version>.zip`, which does not expire
(`.scratch/iso26262-tool-qualification` ticket 01 decides how long evidence has to be kept
beyond that).

## Scope

`src/`, `plugins/` (empty of C so far) and `tools/` in full, same as the
sanitizers (`docs/sanitizers.md`) — `third_party/` is vendored and excluded.
`tools/clang-tidy/run.sh` lints every distinct **(file, flag set)** pair, not every
file, because the code a gate skips is the code built with unusual flags. It
derives the pairs from `bazel aquery` (`mnemonic("CppCompile", ...)`), in three
ways wider than `//src/... + //tools/...` in the default configuration:

- **A file Bazel compiles more than once is linted once per flag set.** `trace.c`
  is built with and without `-DQWE_NO_STRERRORNAME_NP` (`errno_name_test`'s
  variant, which compiles the fallback table); `alloc.c`, `errstr.c` and
  `bcembed.c` are also built in the exec configuration with `-DNDEBUG`, which
  removes their `assert`s. Two compiles that differ only in the name of the
  `bazel-out/<configuration>/` directory are one pair.
- **`manual`-tagged cc targets are added by name.** Bazel's wildcard expansion
  skips them, the rule that keeps them out of `bazel build //...`, so `run.sh`
  finds them with `bazel query 'attr(tags, manual, kind("cc_.* rule", ...))'`
  and says which it added (today `//src/edge/yaml:chain_fuzz` and
  `//src/edge/yaml:transcode_fuzz`, whose sources `fuzz_harness.c`,
  `chain_fuzz.c` and `transcode_fuzz.c` are linted).
- **Every `.bazelrc` configuration that changes what the preprocessor sees is
  enumerated**, one `bazel aquery` each, and the pairs unioned: `coverage`
  (`-DQWE_GCOV`, the `src/kernel/gcov.h` branch nothing else compiles, read from
  `.bazelrc`'s `coverage` lines rather than repeated in `run.sh`), `valgrind`
  (`--define=qwe_valgrind=1`, which adds `valgrind_smoke_test.c`), `ubsan` and
  `asan` (each adds one `-DQWE_SMOKE_*` build of `sanitizer_smoke_test.c`) and
  `fuzz` (`--define=qwe_fuzz=1`). Measured at `995b7b3`, `fuzz` adds no pair the
  default configuration does not have, and is still enumerated, so a `select()`
  or `#ifdef` that starts to key on it is linted from that commit. `release` is
  the one configuration left out: it only adds `-g`, which is not among the
  flags `run.sh` keeps.

A finding is reported once, by `file:line`, with the configurations it appeared
under (`in: src/kernel/proc.c [coverage] -DQWE_GCOV; ...`), however many pairs
reported it.

`run.sh --list` prints the (file, configuration) pairs and their count (186 at
`995b7b3`) without building or running clang-tidy; the gate prints the same
list before it starts, so CI's log and the evidence bundle's `files.txt` show what
was covered. Each line is `file`, the configurations that produced that flag set,
and the variant, the `-D`/`-std` flags that tell it from the file's other pairs.
`tools/clang-tidy/coverage_test.sh` compares the `.c` files on disk under `src/`,
`tools/` and `plugins/` with that list and fails on any file in neither it nor
an allow-list, which is empty. It also fails if `trace.c` loses either variant,
if a fuzz harness source drops out, or if `-DQWE_GCOV` stops being linted. It is not
a Bazel test (it needs `bazel aquery`, which cannot run in a sandbox); CI runs it
in `static-analysis.yml`.

## Headers

`.clang-tidy` sets `HeaderFilterRegex: '.*'` with
`ExcludeHeaderFilterRegex: '(^|/)(third_party|bazel-out|external)/'`: every
header of ours is checked, vendored and Bazel-generated ones are not, and
system headers are skipped by clang-tidy's own default. clang-tidy matches the
filter against the path the header resolved to, which for Bazel's `-iquote .`
is `./src/...` (or absolute), so the earlier `'^(src|tools)/.*'` never matched
and a finding in any of our headers was silently dropped. A narrower
`'/(src|tools)/'` is no fix either: `third_party/luajit/src/*.h` matches it.
Proven by a throwaway `atoi` in `src/kernel/redact.h` and another in
`third_party/greatest/greatest.h` with `cert-err34-c` on: the gate failed on
the first and ignored the second.

## The backlog: `--raw`

```
tools/clang-tidy/run.sh --raw
```

The default mode is a pass/fail gate, and its `--quiet` output ("N warnings
generated", about 1,000 of them) is just what the exclusions below and system
headers suppressed: it says nothing about which checks. `--raw` runs every group
`.clang-tidy` enables with none of its exclusions and none of its
`CheckOptions`, and prints a per-check tally split into `src/`+`tools/` and
`*_test.c`. A header finding is counted once, not once per file that includes
it. It is a measurement, not a gate: it exits 0 whatever it finds.

Each check's row says what hid it from the gate: `excluded` (a `-check` line
in `Checks`) or `option` (a check the gate runs, narrowed by a `CheckOptions`
entry such as `bugprone-reserved-identifier.AllowedIdentifiers`). The last line totals the two.
The options are dropped by passing `--config-file` a copy of `.clang-tidy`
without its `CheckOptions:` block: clang-tidy cannot blank an option on the
command line (`--config` replaces the file rather than merging with it). The
`option` label assumes the gate is at exit 0. Run on a tree that fails the
gate, a finding in an enabled check shows as `option` whatever the cause.

At `acb416d` it counted 619 findings, none in a header;
`.scratch/archive/sca-findings/spec.md` worked through the bug-finding classes.
At `dcc74d8` it counts 427: 254 hidden by an exclusion, every one in a class the
exclusion table below still lists, and 173 hidden by `cert-err33-c`'s
`CheckedFunctions` (all `fprintf`/`fputs`/`fputc`). `.scratch/archive/sca-exclusions`
worked through what is left: with `CheckedFunctions` gone, the count is 181,
80 hidden by an exclusion and 101 by an option (the reserved-identifier allow
list, and `concurrency-mt-unsafe`'s `FunctionSet`, which `--raw` widens to `any`).

## No compile_commands.json, no Python

Every other way to feed Bazel-built flags to `clang-tidy` goes through a tool
that shells out to Python (`hedron_compile_commands`'s `refresh_compile_commands`
is a `py_binary`) — this repo runs none (`CLAUDE.md`). Instead, `run.sh` reads
`bazel aquery`'s JSON directly: it finds each `CppCompile` action
whose arguments contain `-c <file>` (all of them, not the first per file: see
"Scope"), and keeps only the flags `clang-tidy`'s
parser needs (`-iquote`/`-isystem`/`-D`/`-std`). Everything else Bazel's
GCC toolchain passes (`-Wall`, `-frandom-seed`, `-MD`/`-MF`, `-fno-canonical-system-headers`,
...) is GCC-specific plumbing that means nothing to clang and can trip its
driver on a flag it does not recognise. `run.sh` runs `bazel build //src/... //tools/...` first, plus every `genrule` under
`//third_party/...` (found with `bazel query`), so every generated header the compile
actions expect (LuaJIT's `luajit.h` and buildvm output, `src/kernel/lua/embedded.h`) exists
on disk under `bazel-out` before `clang-tidy` reads it. The `third_party` genrules have to be
named: `//src/...` does not reach them, and a warm disk cache with a fresh output base (a
pull request's CI run) serves the compile actions from the cache without ever writing a
header nobody asked for. `run.sh` fails if the query finds no genrule, so the list cannot
silently go empty.

## The ruleset, and every exclusion

`.clang-tidy` at the repo root turns on `clang-analyzer-*`, `bugprone-*`,
`cert-*`, `concurrency-*`, `performance-*` and `portability-*` — the C-applicable
groups (C++-only groups like `cppcoreguidelines-*` and `modernize-*` are left
off; this is a C99 codebase) — with `WarningsAsErrors: '*'`: a finding fails the
gate, no recovery, the same rule as the sanitizers. Getting there from the raw
defaults meant excluding checks that do not fit this codebase, each for a
specific, checked reason:

| Check(s) | Why excluded |
|---|---|
| `cert-dcl51-cpp` | A C++ rule (reserved names in a C++ translation unit). It never fires on C, so excluding it hides nothing. |
| `bugprone-reserved-identifier`, `cert-dcl37-c` (narrowed, not excluded) | `AllowedIdentifiers` lets through the seven reserved names this code has to use, anchored: `_POSIX_C_SOURCE` and `_GNU_SOURCE` (the feature-test macros, required before the first include), `__wrap_*` and `__real_*` (what the `-Wl,--wrap=` OOM-test harness links against, `docs/ci-checks.md`'s "Allocation checks"), `__executable_start` (a linker-defined symbol `oom_shim.c` reads to print call-site offsets), and `__gcov_dump` and `__gcov_reset` (libgcov's entry points, declared in `src/kernel/gcov.h` for the `coverage` configuration, the only one that compiles them). Any other reserved name (a `_Foo` type, a `__helper`) is a finding. |
| `bugprone-multi-level-implicit-pointer-conversion` | All 50 findings (measured at `407f76a`) are a `void *` converted to or from a `T **` by an allocator, `free` or `qsort`, the C idiom of not casting them: 35 are `T **` to `void *` (`free` 27, `qsort` 3, the argument of `realloc` 5), 13 are `void *` to `T **` (the result of `realloc` 5, `calloc` 5, `qwe_xcalloc` 2, `malloc` 1), and 2 more are `qsort` comparators turning `const void *` into `const char *const *`. The check has no options, so the only way to quiet it is a cast at each of the 50. |
| `concurrency-mt-unsafe` (narrowed, not excluded) | `FunctionSet: glibc`: only what glibc documents as thread-unsafe, not the POSIX list (79 findings under the default `any`, 49 under `glibc`; `getenv` and `readdir` are glibc-safe as long as nothing calls `setenv`, and `setenv` is confined to tests below). The 49 were fixed, not waved through: `strerror` (18 in `src/` and `tools/`, 2 in tests) is `qwe_strerror` (`src/kernel/errstr.h`, `strerror_r` into a thread-local buffer), `sigprocmask` (4 and 8) is `pthread_sigmask`, `sleep` (1) is `nanosleep`, and the 14 `setenv`/`unsetenv` in tests go through `src/testing/env.h`. Two `NOLINTNEXTLINE(concurrency-mt-unsafe)` remain, both on the same premise: `qwe_test_setenv`/`qwe_test_unsetenv` there, and the `exit` in `luavm.c`'s Lua panic handler. The premise, that qwe has no threads (`docs/adr/0001-fork-per-plugin-step.md`), is tested: `//src/cli:no_threads_test` fails if the `qwe-debug` binary links `pthread_create` or `thrd_create`, the only way glibc starts a thread, and shows the check can tell by running it on a probe binary that does. |
| `bugprone-easily-swappable-parameters` | 30 findings (21 in `src/` and `tools/`, 9 in tests). No option set leaves only the real hazards. `ModelImplicitConversions: false` drops 6 (`int`/`long`/enum mixes), the 3 `qsort` comparators (`a`, `b`) stay because their signature is fixed by `qsort`, `MinimumLength: 3` drops all of them (none has a run of three), and what is left is 8 test helpers (`write_file(name, body)` four times, `has_file`, `run`, two out-parameter pairs: a swapped call fails the test at once) and 13 findings in 12 functions in `src/` and `tools/`, most with one or two call sites in the same or a neighbouring file (e.g. `send_result(L, idx, fd)`, `qwe_summary_write(path, run_dir, workflow_file, ...)`, `ssh_call(job, fn, a, b, ...)`). Fixing those would mean wrapping arguments in structs, which is a refactor, not a fix of a defect. |

### snprintf: say what a cut means

A call site never ignores `snprintf`'s return. It uses one of three helpers in
`src/kernel/fmt.h` (`//src/kernel:fmt`), picked by what a truncated result
would mean:

- **`qwe_fmt`** for a path, a key or a name, where a cut is a wrong answer. It
  returns -1 on truncation, and the caller fails.
- **`qwe_xfmt`** for a buffer sized to fit (allocated from the lengths going in,
  or a fixed buffer for a number) and for test fixtures. It aborts with
  `file:line: formatted text truncated`, the way `qwe_xmalloc` does on OOM.
- **`qwe_msg`** for a diagnostic message a person reads. A cut only shortens it.

`sprintf` is not used anywhere. `tools/bcembed.c` builds without `src/` and
checks its two `snprintf` calls inline.

### Data streams: check `ferror` once, before `fclose`

A writer of many small pieces does not test each `fputs`. A stdio stream's
error flag is sticky, so it writes freely through `qwe_out_str`, `qwe_out_ch`
and `qwe_out_fmt` (`src/kernel/put.h`, `//src/kernel:put`), which return
nothing and carry the one `(void)` cast, and then checks `ferror(fp)` before
`fclose(fp)`, and `fclose`'s own result. `fclose` alone is not enough: it
reports its own final flush, so a block lost in the middle of the stream and a
tail that flushed cleanly look like success. `result.c`, `summary.c`,
`bcembed.c`, `report_disabled` and the test fixture writers all follow it. A
write whose failure the next step depends on checks its own return instead.

### Diagnostics: `qwe_diag`

A failed write to `stderr` has no one to report to, so ignoring it is right, and
it is decided once. Every `fprintf(stderr, ...)` is `qwe_diag(...)`
(`src/kernel/put.h`, next to the data-stream helpers), which writes the same
bytes and carries the one `(void)` cast. `cert-err33-c` runs with its full
default function list: there is no `CheckedFunctions` option any more.

### Test code: same checks, two idioms

`*_test.c` files get exactly the checks every other file gets. greatest's
`ASSERT`/`FAIL` return out of a test on the first failure, past any `free()` or
`fclose()` below them, so the analyzer's leak and stream checkers would flag
any test that frees after asserting. Tests follow two rules instead:

- **A test does not free or close what it asserts across.** It hands the pointer
  to `qwe_own()` (or a `FILE *` to `qwe_own_file()`) from `src/testing/owned.h`
  (`//src/testing:owned`), and the teardown callback `qwe_release_owned` frees
  and closes everything after each test, pass or fail. Install it with
  `SET_TEARDOWN(qwe_release_owned, NULL);` at the top of `main` and of every
  `SUITE`, because greatest clears the teardown when a suite ends.
- **A setup helper aborts on failure.** A helper that writes a fixture file or
  reads one back (`write_file`, `slurp`, `with_preamble`, `run`) calls `abort()`
  if its `fopen`/`open`/`tmpfile`/`ftell`/`malloc` fails. It does not carry on
  with NULL or -1. An abort fails the test binary outright, which is the right
  outcome for a broken fixture.

A single-line false positive is a `// NOLINTNEXTLINE(<check>)` with a comment
explaining why (one exists in `read_file`, `src/kernel/workflow.c`); a
whole-codebase false positive is a `.clang-tidy` exclusion in the table above,
not a scattering of `NOLINT`s.

## What the gate found on first run

Real bugs, fixed as part of standing this gate up, not pre-existing findings
suppressed to get a green run:

- **`child_argv` (`src/kernel/workflow.c`) leaked its built `argv`** (and every
  `strdup`'d argument in it) on two error returns after the array was built:
  the status-pipe-write failure and the stdin-setup failure. Only the
  out-of-memory path already freed it. Found by `clang-analyzer-unix.Malloc`.
- **`log_tail` (`src/kernel/summary.c`) and `slurp` (`tools/bcembed.c`)** computed
  a buffer size from `ftell()` without checking for its `-1` failure return.
  `(size_t)-1 + 1` wraps to `0`; in `log_tail` this reaches a `buf[len] = '\0'`
  with `len == -1`, a one-byte write before the allocated block. Found by
  `clang-analyzer-unix.Errno` and `clang-analyzer-optin.portability.UnixAPI`
  (`malloc` of 0 bytes).
- **`qwe_oom_probe`'s forked child (`src/kernel/oom_shim.c`)** called
  `dup2(nul, 0)` without checking that `open("/dev/null", ...)` succeeded.
  Found by `clang-analyzer-unix.StdCLibraryFunctions`.
- Two `qsort(ptr, n, ...)` calls (`src/kernel/jobs.c`, `src/kernel/validate.c`)
  passed a possibly-null `ptr` when `n == 0` (the array is never allocated for
  an empty list) — a base pointer that must not be null even when the
  standard permits `nmemb == 0`. Found by `clang-analyzer-core.NonNullParamChecker`.

## What linting every configuration found

`.scratch/sca-round3` ticket 05 widened the gate from the first compile action of
each file in the default configuration to every distinct (file, flag set) pair
(see "Scope"). The code it newly reached produced three findings, and no others:

- **`__gcov_dump` and `__gcov_reset`** in `src/kernel/gcov.h`, reported by
  `bugprone-reserved-identifier` and `cert-dcl37-c` under `coverage`
  (`-DQWE_GCOV`), the only configuration that compiles that branch. The names are
  libgcov's and cannot change, so they are allowed as exact names in
  `AllowedIdentifiers` (ticket 15 moves them into a directory-scoped config).
- **`valgrind_smoke_test.c`'s deliberate uninitialised read**, reported by
  `clang-analyzer-core.UndefinedBinaryOperatorResult` under `valgrind`, the only
  configuration that builds the file. It is a true positive on purpose: the test
  commits the fault so valgrind has something to catch. Hiding it (a `volatile`
  pointer, an indirect `malloc`) does not work, the analyzer traces the value back
  to `malloc`, so it carries one `NOLINTNEXTLINE` with that reason.

A throwaway `atoi` (`cert-err34-c`) proves each newly covered branch is live: in
`trace.c`'s production `#else` branch, in its fallback table, and in `gcov.h`
under `#ifdef QWE_GCOV`, each fails the gate and each is reported under the pair
that compiles it, `(default)`, `(default) -DQWE_NO_STRERRORNAME_NP ...` and
`(coverage) -DQWE_GCOV`.

## What re-enabling excluded checks found

Real bugs behind checks that had been excluded as false positives, found by
`.scratch/archive/sca-findings` as each class was switched back on:

- **`lifecycle_test.c` handed greatest stack buffers as failure messages.**
  `ASSERTm(where, ...)` keeps the message pointer in a global and prints it
  after the test function has returned, so every one of the 24 `ASSERTm`s
  on a local `char where[]`/`why[]`/`line[]` read a dead stack frame on failure.
  The buffers are now `static`. Found by `clang-analyzer-core.StackAddressEscape`,
  which had been excluded as a greatest false positive.
- **`preamble_test.c`'s `with_preamble` returned NULL on a setup failure and
  left the length unset**, and every caller passed both straight to `run()`, so
  a `qwe_preamble_build` failure became a `write()` of an uninitialized length
  from a null pointer, not a test failure. It now aborts, as `run()` already
  does on its own setup failures. Found by `clang-analyzer-core.CallAndMessage`.
- **Test fixture helpers ignored their own failures.** `write_file` in
  `src/cli/validate/oom_test.c`, `src/kernel/load_oom_test.c`,
  `src/kernel/oom_test.c` and `src/kernel/workflow_test.c` passed an unchecked
  `fopen` to `fputs`. So did `oom_test.c`'s `fresh_select_case`, and
  `src/cli/encrypt/oom_test.c`'s `main` did the same with `fwrite`.
  `load_oom_test.c`'s `validate_failing` passed an unchecked `open` to `dup2`,
  and `preamble_test.c`'s `run` passed an unchecked `tmpfile`/`dup` to `fwrite`
  and `lseek`. `summary_test.c`'s `slurp` checked neither `fopen` nor `ftell`,
  so a failed `ftell` became `malloc(0)` followed by a write one byte before
  the block (the same bug the first run fixed in `summary.c`'s `log_tail`).
  `envelope_test.c` wrote into an unchecked `strdup`. Each of these would crash
  inside libc instead of failing the test. The helpers now abort. Found by
  `clang-analyzer-core.NonNullParamChecker`,
  `clang-analyzer-unix.StdCLibraryFunctions` and
  `clang-analyzer-optin.portability.UnixAPI` once `run.sh` stopped narrowing
  them out of test files.
- **A failed write of `result.json` leaked its stream.** `qwe_run_workflow`
  tested `!fp || qwe_result_write(...) < 0 || fclose(fp) != 0`, so a write
  error short-circuited past the `fclose`. The stream is now closed whether
  or not the write failed. Found by `cert-err33-c` (`fclose`).
- **`bcembed` ignored a failed write of its output.** It never checked the
  final `fclose`, so a full disk left a truncated module table that still
  compiled into the binary. It now exits 1 with `cannot write <out>`
  (`//tools:bcembed_test` writes to `/dev/full`). `slurp` there and
  `log_tail` in `summary.c` also sized their buffer from an `ftell` after an
  unchecked `fseek`. Found by `cert-err33-c` (`fclose`, `fseek`).
- **`gmtime_r`/`strftime` results were used unchecked.** `result.json`'s
  `put_time` and the run-id formatter printed whatever the buffer held when
  a time could not be broken down (a year past `INT_MAX`). `put_time` now
  writes the epoch seconds as the string (`result_test`'s
  `time_past_gmtime_written_as_seconds`), and the run id falls back to them.
  Found by `cert-err33-c`.
- **`qwe_summary_write` reported a summary as written when a block in the
  middle was lost.** It checked only `fclose`, which reports a failure of its
  own final flush and not an earlier failed write, so a stream that failed one
  write and took every later one returned 0. `summary_test`'s
  `mid_stream_write_failure_fails` writes through a `fopencookie` stream whose
  first write fails (red against the `fclose`-only check, green with
  `ferror`), through the new `qwe_summary_render(FILE *, ...)`. `report_disabled`
  now checks `ferror` on its memstream as well. Found by `cert-err33-c`
  (`fputs`, `fprintf`).
- **`report_disabled` printed a memstream's buffer after an unchecked
  `fclose`.** `open_memstream` sets the buffer only on a clean close, so a
  failure there passed NULL to `%s`. And a plugin step's child reported
  success even when the final `fflush(stdout)` failed and its output never
  reached the log. It now exits 1. Found by `cert-err33-c` (`fclose`,
  `fflush`).
- `clock_gettime(CLOCK_MONOTONIC)` (six sites) and `qwe_timer_disarm`'s
  `timerfd_settime` went unchecked. The clock goes through
  `src/kernel/clock.h`'s `qwe_mono_now()`, which aborts on the failure Linux
  cannot produce rather than time a step from an unset `timespec`.
  `qwe_timer_disarm` now returns the result, like `qwe_timer_arm`.
- **`qwe_key_generate` copied its path into a 1024-byte buffer unchecked.**
  The copy is used to `mkdir` each parent. A longer path was cut short, so
  qwe made directories along the truncated prefix and then failed with a
  misleading "cannot create the key file". It is now refused up front with
  "the key file path is too long" (`keyfile_test`'s
  `generate_refuses_an_overlong_path`). `qwe_key_default_path` already
  failed on an over-long `HOME`, but `qwe keygen` and `resolve` reported
  that as "HOME is not set". They now say it is too long
  (`tests/e2e/keygen_long_home`). Found by `cert-err33-c` (`snprintf`).
- **`bcembed` wrapped a JSON module with `sprintf` into an unchecked
  `malloc`**, and a module name over 255 bytes was silently cut short in the
  chunk name. Both now fail the build. Found by `cert-err33-c` (`sprintf`,
  `snprintf`).
- The five other `sprintf`s (`validate.c`'s JSON pointers, `workflow.c`'s run
  directory and trace path) and `sink.c`'s line prefix were bounded only by
  allocation arithmetic kept next to them by hand. They are now `qwe_xfmt`
  with the size passed in, so a future mismatch aborts instead of
  overflowing.
- **`QWE_TEST_GRACE_MS` was parsed with `atol`**, so a typo (`3OO`) gave the
  run 0 ms of grace between SIGTERM and SIGKILL. It is now parsed with
  `strtol` and checked. A value that is not a whole, non-negative number is
  ignored with a warning, and the 10 s default stands
  (`tests/e2e/grace_env_malformed`). `proc_test.c`'s `sscanf`/`atoi` reads of
  `/proc/<pid>/stat` and child output go through two strict parsers, so a
  misparse fails the test instead of passing with a wrong value. Found by
  `cert-err34-c`.
- **`log_tail` (`summary.c`) went on after an unchecked `rewind()`.** The
  `clang-analyzer-unix.Errno` hit had been written off as an
  "inter-procedural false positive" that traced into an unrelated loop. With
  the analyzer's full path (`qwe_summary_write` → `log_tail`), the flagged
  call is `rewind`. It returns nothing, so `errno` is the only report of a
  failure, and nothing read it before `malloc` could overwrite it. The
  earlier `ftell` fix sat on the line above. `rewind` is replaced by a checked
  `fseek(..., SEEK_SET)` there and in `summary_test.c` and `corpus_test.c`,
  the other two hits. The check only fires with `unix.StdCLibraryFunctions`
  on, because that checker models which calls set `errno`, so running it
  alone finds nothing.
- `strcpy`/`strcat` are gone from `src/` and `tools/`. `load_inventory`'s
  default path, the run directory's `/result.json`, the trace's `-` step
  and `redact_test.c` each use `memcpy` of a length already known, or
  `qwe_xfmt` with the buffer's size. The exclusion was name-based, so it would
  also have let a fourth, unbounded call through.
- **`bcembed` sized its buffers from whatever file it was given.** Found by
  `clang-analyzer-optin.taint.TaintedAlloc`. It now refuses a source over
  1 MiB (about 30 times the largest module, luacheck's `parser.lua`) with
  `cannot read <file>: File too large` (`//tools:bcembed_test`).
