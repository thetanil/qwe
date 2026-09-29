# 08: Compiler warnings the toolchain leaves off

Status: resolved
Category: enhancement
Type: task

## What

`.bazelrc` builds with `-std=c99 -Wall -Wextra -Werror`. The compiler is the
cheapest analyzer there is and already runs on every push. These extra flags were
run over `//src/... //tools/...` with `-Wno-error` on 2026-09-26 (`bb9d69f`):

| Flag | src+tools | tests | Where |
|---|---|---|---|
| `-Wwrite-strings` (shows as `-Wdiscarded-qualifiers`) | 0 | 78 | `run_test.c` 40, `validate_test.c` 19, `proc_test.c` 12, `run/oom_test.c` 4, `validate/oom_test.c` 2, `encrypt/oom_test.c` 1: a string literal stored in a `char *` |
| `-Wcast-qual` | 6 | 4 | `jobs.c` 3, `validate.c` 2, `positions.c` 1; `validate/oom_test.c` 3, `transcode_test.c` 1 |
| `-Wsign-conversion` | 3 | 2 | `workflow.c` 2, `sched.c` 1; `sched_test.c`, `proc_test.c` |
| `-Wmissing-prototypes` | 6 | 15 | `oom_shim.c` 5 (the `__wrap_*` definitions), `transcode.c` 1; tests 15 |
| `-Wbad-function-cast` | 2 | 0 | `workflow.c:1455,1896` (`(long)lua_tonumber(...)`) |
| `-Wfloat-equal` | 1 | 0 | `luacbor.c:337` |
| `-Wswitch-default` | 1 | 0 | `workflow.c:1116` |
| `-Wformat=` (from `-Wformat=2`) | 0 | 1 | `lifecycle_test.c:277`: `%d` for an `unsigned` |
| `-Wpedantic` | 0 | 2 | `lifecycle_test.c:243,247`: a compound literal in a static initializer (ticket 07 of round 2 introduced them) |
| `-Wswitch-enum` | 88 | 0 | separate: ticket 09 |

Zero findings for `-Wshadow`, `-Wconversion`, `-Warith-conversion`, `-Wformat=2`,
`-Wformat-signedness`, `-Wnull-dereference`, `-Wduplicated-cond`,
`-Wduplicated-branches`, `-Wlogical-op`, `-Wstrict-prototypes`,
`-Wold-style-definition`, `-Wvla`, `-Walloca`, `-Wundef`, `-Wredundant-decls`,
`-Wcast-align`, `-Wpointer-arith`, `-Wjump-misses-init`, `-Wunused-macros`,
`-Wstrict-overflow=2`, `-Wdouble-promotion`, `-Wtrampolines`,
`-Wfree-nonheap-object`. A zero costs nothing to turn on and stops a regression,
so turn them all on. Note the Bazel default toolchain passes
`-Wno-free-nonheap-object` (visible in every compile command); appending
`-Wfree-nonheap-object` after it re-enables the diagnostic, and it currently
has nothing to say. (The one warning left out: `-Wstack-protector` reports
"all local arrays are less than 8 bytes" in two tests; it is informational, not a
defect, and a flag that needs a comment is one to leave off.)

## Fix

Fix every finding by changing the code: `const char *` for a literal, drop or
localise a cast (`lua_tonumber` result: assign to a `lua_Number`, then range-check
and convert), `%u`, a `default:` (ticket 09 handles the rest of the switches),
`static` for a function used in one file, a prototype in a header for one used in
two, a `size_t` where a `size_t` is meant. `-Wfloat-equal` at `luacbor.c:337`
compares a double to an integer conversion of itself to decide "is this an
integer": write that as a helper with a named purpose that checks
representability, not `==`. The `__wrap_*` prototypes belong in
`oom_shim.h` (ticket 15 moves them).

Then put the flags in `.bazelrc` with `-Werror`, as the same line as `-Wall
-Wextra`, **and** in the exec configuration: today `--host_copt` carries only
`-std=c99`, so `tools/bcembed.c` and the exec build of `errstr.c` are compiled
with no warnings at all. Add `--host_copt` for the same set (or move the flags
into the shared copts constant if ticket 13 chose that route).

**Measure at the optimisation level that ships too.** The counts above are from
the default fastbuild (`-O0`). `-Wnull-dereference`, `-Wstrict-overflow`,
`-Wmaybe-uninitialized` and parts of `-Wformat-overflow`/`-Wstringop-*` depend on
optimisation passes and are close to silent at `-O0`, so a zero there is not
evidence. Rerun with `--copt=-O2` over `//src/... //tools/...` (our code only;
LuaJIT's `-O0` in its own BUILD stays, see the `.bazelrc` note on the -O2
miscompile), fix what it finds, and add a CI build at `-O2` with the flags so
those warnings keep being checked (the release build stays at fastbuild; this is
a warnings-only build).

## Acceptance criteria

- [x] `.bazelrc` builds `src/`, `tools/` with all the flags above (except
      `-Wswitch-enum`, ticket 09), `-Werror`, and appends `-Wfree-nonheap-object`.
      `manual: .bazelrc; bazel build //...`
- [x] Zero warnings under those flags for `src/` and `tools/` and tests, with no
      `#pragma GCC diagnostic` and no per-file `-Wno-`. `manual: bazel build
      --keep_going --copt=-Wno-error //...` prints none
- [x] The same flags reach the exec configuration. `manual: bazel aquery` on
      `//tools:bcembed` shows them on the exec action
- [x] Zero warnings under those flags at `-O2` too, and a CI job builds that way.
      `manual: bazel build --keep_going --copt=-Wno-error --copt=-O2 //src/... //tools/...`
      prints none; the workflow is named in the Comments
- [x] `third_party/` keeps its own flags (`-w` in its BUILD `copts`, and the
      `per_file_copt` rules in `.bazelrc`, as today) and is not changed.
      `manual: git diff --stat third_party` empty
- [x] Each fix that changes behaviour (the `(long)lua_tonumber` conversions,
      `luacbor.c:337`) has a test at the boundary values. `unit: <tests>`
- [x] Any finding the analyzer of ticket 06 or 07 also reports is cross-referenced
      in the Comments.
- [x] `bazel test //...` (also `--config=asan`, `--config=ubsan`) and the coverage
      check are green.

## Comments

Worked on `sca-round3/08-compiler-warnings`, from `origin/main` at `47edde6`, gcc
`13.3.0-6ubuntu2~24.04.1`, clang 20.1.8 for the fuzz build.

### Measured again

Bazel prints a warning only when it runs the action, so each measurement changed every
command line with a unique `--copt=-DQWE_WARN_PROBE_<n>` (a cached action replays nothing,
and a warm build looks clean). With the flags on `--copt` over `//...`, the ticket's table
reproduced for our code, plus two:

- `-Wshadow` 1, not 0: `luacbor.c:396`, `int r, idx;` inside `qwe_lua_to_cbor` shadows its
  `idx` parameter (added since `bb9d69f`). Renamed `own`.
- `-O2`, `//src/... //tools/...`: one more, `-Wformat-truncation=` in `fmt_test.c`'s
  `msg_cuts` (`qwe_msg` into `char[4]` of `"error %d"`, inlined and proven cut). The cut is
  what that test checks: the text is now a `const char *volatile`, printed with `%s`, so the
  compiler cannot prove it.

`third_party/` warned on almost every flag (libyaml, tinycbor): that is why the set is not a
`--copt` (below).

### Where the flags go

- `.bazelrc`: `--per_file_copt=.*,-third_party/.*@<the set>` beside the unchanged `-Wall
  -Wextra -Werror` line, and `--host_per_file_copt` with the same set plus
  `--host_copt=-Wall/-Wextra/-Werror` for the exec configuration. Not the same `--copt` line
  as the ticket put it: that line reaches `third_party/`, and criterion 5 keeps third_party's
  flags as they are. The per-file form is the one `.bazelrc` already uses to scope
  (`build:ubsan`). **Trap:** Bazel matches the regex against the whole path, so
  `-^third_party/` (my first try) excluded nothing; `-third_party/.*` does.
- `-Wfree-nonheap-object` lands after the toolchain's `-Wno-free-nonheap-object` (aquery of
  `//src/kernel:errstr`: the toolchain's `-Wno-` early in the command line, ours near the
  end), so it is on.
- Exec: `bazel aquery 'mnemonic("CppCompile", //tools:bcembed)'` shows `-Werror`, `-Wextra`,
  `-Wwrite-strings`, `-Wfree-nonheap-object` on both `k8-opt-exec` actions. The exec build
  is at `-O2`, so `bcembed.c`, `alloc.c`, `errstr.c` and `put.c` get the `-O2` check there too.
- `build:fuzz` adds `--copt=-Wno-unknown-warning-option` (and `--host_copt`): the set is
  gcc's, and clang errors on six names it lacks under `-Werror`. It is a flag about flag
  names, not a finding silenced. clang then checks the rest and found two gcc does not:
  `fmt.h:28` `-Wformat-nonliteral` (`qwe_vfmt` had no `format(printf, 3, 0)`: added) and
  `luacbor.c:172` `-Wdouble-promotion` (a `float` to `lua_pushnumber`: now an explicit
  `(lua_Number)f`).
- `build:warnings-o2 --copt=-O2`, and `compiler-warnings.yml` running
  `bazel build --config=warnings-o2 //src/... //tools/...` on push, pull request, nightly
  and release. It is in `docs/ci-checks.md`'s table, "Every push" and the pull request
  gates (nine job names, with 07's `gcc-analyzer`). `docs/compiler-warnings.md` is
  new; `docs/static-analysis.md` and `run.sh` say `warnings-o2` is left out of clang-tidy's
  configurations for the same reason as `release`.

### The fixes

| Finding | Change |
|---|---|
| `-Wwrite-strings`, CLI tests (73) | The commands only read `argv`: `qwe_cmd_fn`, `qwe_dispatch` and the five `qwe_cmd_*` take `const char *const *argv`; `main` makes the one cast (adding `const` at both levels, which C does not do implicitly and `-Wcast-qual` allows). Tests declare `const char *argv[]`. |
| `-Wwrite-strings`, `proc_test.c` (5) | `qwe_child_fn` returns what `execvp` takes (`char **`), so the tests' argv are `static char` arrays. |
| `-Wcast-qual` `jobs.c` (3) | `qwe_step_result`'s `id`, `name`, `plugin` are `char *`, like `outputs_json` beside them: the result owns them (`qwe_jobs_free` frees them). `result_test`/`summary_test` use `char` arrays. |
| `-Wcast-qual` `validate.c` (2) | `check_dag` keeps the owning `char **` lists in `needs_of[]` and gives the dag a `const char *const *` view. One more checked `calloc`: `alloc_audit.txt` 13 → 14. |
| `-Wcast-qual` `positions.c` | `bsearch` with the pointer string as the key (`cmp_key`), no probe entry. |
| `-Wcast-qual` tests | `validate/oom_test.c`: scenarios not `const` (the probe takes `void *`). `transcode_test.c`: the value is `const yaml_char_t[]`. |
| `-Wsign-conversion` `sched.c`, `workflow.c` (×2), `sched_test.c` | `running += is_running(...)` (int into `size_t`) is `if (...) running++`. `proc_test.c`: `room` is `rlim_t`. |
| `-Wmissing-prototypes` | `oom_shim.h` declares the five `__wrap_*` (ticket 15 may move them); the six tests with their own wrappers declare them beside their `__real_*`; `transcode.c` includes `transcode_hooks.h`. |
| `-Wbad-function-cast` `workflow.c` ×2 | `qwe_count_from_number` (`jobs.c`): below 1 and NaN → 0, from 2^63 up → `LONG_MAX`, else `(long)v`. `(long)1e300` was undefined; on x86 it gave `LONG_MIN`, which both callers read as "no limit" by accident. |
| `-Wfloat-equal` `luacbor.c` | `is_exact_integer(d)`: the range test (±2^53) first, which also rejects NaN and ±inf, then `fpclassify(modf(d, &whole)) == FP_ZERO`. The old `d == (double)(long long)d` converted before checking the range, undefined for NaN, ±inf and \|d\| ≥ 2^63. |
| `-Wswitch-default` `workflow.c` `apply_action` | Every action is listed; the `default` prints the value, file and line and aborts (ticket 09's form). |
| `-Wformat=` `lifecycle_test.c:277` | `%u`. |
| `-Wpedantic` `lifecycle_test.c:243,247` | `SCENARIO`'s `steps[]` is automatic, not `static`: a compound literal is a valid initializer there. Brace initializers in the macros would also have worked, and clang-tidy's `bugprone-macro-parentheses` then flags the unparenthesised `pl` (tried: the gate went red). |

No `#pragma GCC diagnostic`, no per-file `-Wno-` added. The one per-target `-Wno-` under
`src/` (`valgrind_smoke_test`'s `-Wno-maybe-uninitialized`) predates this ticket: the
uninitialised read is that test's point, and it builds only under `--config=valgrind`.

Behaviour changes, with boundary tests:

- `jobs_test` `count_conversion_is_total`: 1, 1.9, 8, 2^62, 2^63 − 1024 (the largest double
  below 2^63) → `LONG_MAX − 1023`, 2^63, 1e300, +inf → `LONG_MAX`; 0.5, 0, −1, −1e300, −inf,
  NaN → 0.
- `lua_cbor_test` `encode_integers_at_the_boundaries`: ±2^53 and 2^53 − 1 exact CBOR integer
  bytes, −1, 0, −0.0 → integer 0; 2^53 + 2, −2^53 − 2, ±2^63, 1e300, 0.5, −0.5, 2^51 + 0.5,
  ±inf and NaN → a double (`0xfb`). (My first version had 2^52 + 0.5, which is not a double:
  the ulp there is 1, so it rounds to 2^52 and the test failed on its own input.)

### Cross-reference with tickets 06 and 07

None of these findings is one the analyzers reported. The nearest: 06's `jobs.c:186` false
positive (NULL `jobs` in `qwe_jobs_free`) is in the same function as the three `jobs.c`
casts, but a different defect. 07's findings (`oom_shim.c`, `luaexec.c`, `jobs.c:134`,
`encrypt.c`) touch none of these lines. Both 07 and this ticket edit `oom_shim.c`/`.h`
neighbourhoods and `docs/ci-checks.md`; this branch was rebased onto 07 once it merged
(`docs/ci-checks.md` was the one conflict).

### Checks

- `bazel build --keep_going --copt=-Wno-error --host_copt=-Wno-error --copt=-DQWE_WARN_PROBE_<n> //...`:
  no warnings. The same with `--copt=-O2` over `//src/... //tools/...`, and over `//...`: none.
- `bazel build --config=fuzz --config=asan|ubsan //src/edge/yaml:transcode_fuzz //src/edge/yaml:chain_fuzz`: builds.
- `git diff --stat third_party`: empty.
- `bazel test //...`: 265 passed, 3 skipped. `--config=asan` and `--config=ubsan`: 266 passed, 2 skipped.
- `--config=valgrind` on the touched tests (`jobs`, `lua_cbor`, `fmt`, `proc`, `sched`, `summary`,
  `result`, `lifecycle`, `//src/edge/yaml:all`, `//src/cli/...`, the oom sweeps included): 22 passed.
- `tools/clang-tidy/run.sh`: exit 0 (186 pairs).
- `bazel run //tools/coverage:check`: every C file at or above 85%; it fails locally on
  `plugins/builtin/backend-ssh/ssh.lua` (84.7%) and `src/kernel/lua/become.lua` (81.2%)
  only, as on ticket 07: the devcontainer's ssh target fails host-key verification, so the
  ssh e2e cases skip. CI's coverage job runs its own sshd.
- `//tools/ci:workflows_test` passes with the new command and job.

### Own workflows, own badges

After #10 merged: a README badge is per workflow file, so a job inside `static-analysis.yml`
had none of its own, and `static-analysis`'s badge went red for any of three different
checks. `gcc-analyzer` (from ticket 07) and `compiler-warnings` are now their own
`gcc-analyzer.yml` and `compiler-warnings.yml`, with the same triggers as the other gates
(push to main, pull request, `workflow_dispatch`, `workflow_call`), each with a badge in the
README's SCA row. `nightly.yml` and `release.yml` call both, and `release.yml`'s `publish`
needs both. `//tools/ci:workflows_test` rule 4 now requires nightly to call seven gates and
release eight. The job names on a PR are unchanged, so the required-check list in
`docs/ci-checks.md` stays nine names.
