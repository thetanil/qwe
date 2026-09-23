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
It runs in `valgrind.yml` (job `static-analysis`), on the same cadence as valgrind:
by hand, nightly, and in a release, never on a push (see `docs/ci-checks.md`).

## Scope

`src/`, `plugins/` (empty of C so far) and `tools/` in full, same as the
sanitizers (`docs/sanitizers.md`) — `third_party/` is vendored and excluded.
`tools/clang-tidy/run.sh` derives the exact file list from `bazel aquery`
(`mnemonic("CppCompile", //src/... + //tools/...)`), so a `manual`-tagged
target (the libFuzzer binaries) is never linted: Bazel's own wildcard
expansion skips it, the same rule that keeps it out of `bazel build //...`.

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
`.clang-tidy` enables with none of its exclusions and no test narrowing, and
prints a per-check tally split into `src/`+`tools/` and `*_test.c`. A header
finding is counted once, not once per file that includes it. It is a
measurement, not a gate: it exits 0 whatever it finds. At `acb416d` it counted
619 findings, none in a header; `.scratch/sca-findings/spec.md` works through
them one class at a time.

## No compile_commands.json, no Python

Every other way to feed Bazel-built flags to `clang-tidy` goes through a tool
that shells out to Python (`hedron_compile_commands`'s `refresh_compile_commands`
is a `py_binary`) — this repo runs none (`CLAUDE.md`). Instead, `run.sh` reads
`bazel aquery`'s JSON directly: it finds, per file, the `CppCompile` action
whose arguments contain `-c <file>`, and keeps only the flags `clang-tidy`'s
parser needs (`-iquote`/`-isystem`/`-D`/`-std`). Everything else Bazel's
GCC toolchain passes (`-Wall`, `-frandom-seed`, `-MD`/`-MF`, `-fno-canonical-system-headers`,
...) is GCC-specific plumbing that means nothing to clang and can trip its
driver on a flag it does not recognise. `run.sh` runs `bazel build //src/...
//tools/...` first so every generated header the compile actions expect
(LuaJIT's buildvm output, `src/kernel/lua/embedded.h`) exists on disk under
`bazel-out` before `clang-tidy` reads it.

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
| `bugprone-reserved-identifier`, `cert-dcl37-c`, `cert-dcl51-cpp` | Flags `_POSIX_C_SOURCE`/`_GNU_SOURCE` (a required feature-test-macro idiom) and `__real_*`/`__wrap_*` (required by the `-Wl,--wrap=` OOM-test harness, `docs/ci-checks.md`'s "Allocation checks"). Both are reserved-namespace by necessity, not a defect. |
| `bugprone-multi-level-implicit-pointer-conversion` | Fires on every `calloc`/`free` call (`void *` &harr; `T **`) — the standard, recommended C idiom of not casting `malloc`/`calloc`/`free`. |
| `cert-err33-c` | Its default `CheckedFunctions` list is nearly the whole of `<stdio.h>` and `<string.h>`; allocation return values are already independently enforced by this repo's own OOM-injection gate (`quality/02`, `quality/08`), and unchecked `fprintf`/`fputs` to a diagnostic stream is not a defect. |
| `bugprone-implicit-widening-of-multiplication-result` | Every hit multiplies small compile-time-constant macros (`1024 * 1024`, `64 * 1024`, `2 * QWE_YAML_MAX_DEPTH`); the check does not special-case a constant-folded multiplication that cannot overflow. |
| `concurrency-mt-unsafe` | qwe has no threads — concurrency is one process per plugin step (`docs/adr/0001-fork-per-plugin-step.md`) — so "not thread-safe" does not apply. |
| `bugprone-easily-swappable-parameters` | A subjective refactor suggestion (reorder or wrap parameters), not a correctness check. |
| `bugprone-assignment-in-if-condition` | A deliberate, common idiom in this codebase (`if ((out = fopen(...)))`-style single-read checks). |
| `bugprone-misplaced-widening-cast` | Its only hits are test-only `rlim_t` fd-limit setup with values nowhere near overflow. |
| `bugprone-unsafe-functions`, `cert-msc24-c`, `cert-msc33-c` | Only ever `rewind()` (no `gets()` anywhere in the tree) — a `fseek`-has-error-detection style preference, not a defect. |
| `cert-msc30-c`, `cert-msc32-c`, `cert-msc50-cpp`, `cert-msc51-cpp` | `rand()`'s "insufficient randomness" — only used for test-only scheduling-jitter simulation; the actual crypto (`src/secrets`) is libsodium's, not `rand()`'s. |
| `cert-err34-c` | `sscanf`/`atoi` reading kernel-produced `/proc/<pid>/stat` text or a test's own child output — trusted input, not attacker-controlled. |
| `clang-analyzer-optin.performance.Padding` | A performance-only field-order suggestion, not a bug; opt-in for a reason. |
| `clang-analyzer-optin.taint.TaintedAlloc` | False positive for `tools/bcembed.c`, a local, single-user, build-time-only tool — its argv is the build's own module list, not attacker-controlled. |
| `clang-analyzer-security.insecureAPI.strcpy` | Name-based (bans `strcpy`/`strcat` outright); every call site in this tree is bounded by explicit buffer-size arithmetic checked by hand (`src/kernel/workflow.c`'s `load_inventory` and its run-directory path). |
| `clang-analyzer-unix.Errno` | Its one hit (`src/kernel/summary.c`) traced into an unrelated loop in a different function with no `errno` in the flagged file at all — an inter-procedural false positive. |

`run.sh` additionally narrows `clang-analyzer-unix.Malloc`, `clang-analyzer-unix.Stream`,
`clang-analyzer-core.NonNullParamChecker`, `clang-analyzer-unix.StdCLibraryFunctions`
and `clang-analyzer-optin.portability.UnixAPI` for `*_test.c` files only (an extra
`--checks=` argument, appended to `.clang-tidy`'s, `--dump-config` confirms the two
merge rather than one replacing the other). Every one of those checkers reads a
`greatest.h` `ASSERT`/`FAIL` early return — deliberate, so the harness moves on to
the next test instead of running more code past a proven-wrong state — as a leak,
a null deref, or an unclosed stream. A real bug of that shape in a test still fails
under valgrind and the sanitizers, which run the test rather than only read it.
The same checkers keep full value in `src/` and `tools/`, which is where they
found what this ticket actually fixed (below).

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

## What re-enabling excluded checks found

Real bugs behind checks that had been excluded as false positives, found by
`.scratch/sca-findings` as each class was switched back on:

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
