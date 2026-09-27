# 08: Compiler warnings the toolchain leaves off

Status: ready-for-agent
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

- [ ] `.bazelrc` builds `src/`, `tools/` with all the flags above (except
      `-Wswitch-enum`, ticket 09), `-Werror`, and appends `-Wfree-nonheap-object`.
      `manual: .bazelrc; bazel build //...`
- [ ] Zero warnings under those flags for `src/` and `tools/` and tests, with no
      `#pragma GCC diagnostic` and no per-file `-Wno-`. `manual: bazel build
      --keep_going --copt=-Wno-error //...` prints none
- [ ] The same flags reach the exec configuration. `manual: bazel aquery` on
      `//tools:bcembed` shows them on the exec action
- [ ] Zero warnings under those flags at `-O2` too, and a CI job builds that way.
      `manual: bazel build --keep_going --copt=-Wno-error --copt=-O2 //src/... //tools/...`
      prints none; the workflow is named in the Comments
- [ ] `third_party/` keeps its own flags (`-w` in its BUILD `copts`, and the
      `per_file_copt` rules in `.bazelrc`, as today) and is not changed.
      `manual: git diff --stat third_party` empty
- [ ] Each fix that changes behaviour (the `(long)lua_tonumber` conversions,
      `luacbor.c:337`) has a test at the boundary values. `unit: <tests>`
- [ ] Any finding the analyzer of ticket 06 or 07 also reports is cross-referenced
      in the Comments.
- [ ] `bazel test //...` (also `--config=asan`, `--config=ubsan`) and the coverage
      check are green.

## Comments
