# Compiler warnings

```
bazel build //...
bazel build --config=warnings-o2 //src/... //tools/...
```

The compiler is the cheapest analyzer there is, and it already runs on every build.
`.bazelrc` gives our code `-std=c99 -Wall -Wextra -Werror` and, on top of that
(`sca-round3/08`):

```
-Wwrite-strings -Wcast-qual -Wsign-conversion -Wmissing-prototypes -Wbad-function-cast
-Wfloat-equal -Wswitch-default -Wformat=2 -Wpedantic -Wshadow -Wconversion
-Warith-conversion -Wformat-signedness -Wnull-dereference -Wduplicated-cond
-Wduplicated-branches -Wlogical-op -Wstrict-prototypes -Wold-style-definition -Wvla
-Walloca -Wundef -Wredundant-decls -Wcast-align -Wpointer-arith -Wjump-misses-init
-Wunused-macros -Wstrict-overflow=2 -Wdouble-promotion -Wtrampolines
-Wfree-nonheap-object
```

A flag with nothing to say today is on anyway: it costs nothing and stops the
regression. `-Wswitch-enum` is ticket 09's. `-Wstack-protector` is left off: it
reports "all local arrays are less than 8 bytes", which is information, not a defect.

## Scope

- **Everything but `third_party/`**, through `--per_file_copt=.*,-third_party/.*@...`.
  The vendored code keeps the flags its own `BUILD` files give it (`-w` for LuaJIT
  and lpeg, `-Wno-error` and a few `-Wno-` for libyaml, `-Wall -Wextra` alone for
  tinycbor); upstream's style is upstream's. Bazel matches that regex against the
  whole path, so the exclusion is `third_party/.*`, not `^third_party/`, which matches nothing.
- **Tests too.** A test is code that has to be right; the same warnings apply.
- **The exec configuration too.** `--host_copt` and `--host_per_file_copt` carry the
  same set, so `tools/bcembed.c` and the exec builds of `alloc.c`, `errstr.c` and
  `put.c` are checked, at the exec configuration's `-O2`.
- **`-Wfree-nonheap-object` comes last.** The default toolchain passes
  `-Wno-free-nonheap-object`; a per-file copt is appended after it and turns the
  diagnostic back on.
- **clang.** The set is gcc's. `--config=fuzz` builds with clang, which has no
  `-Wduplicated-cond`, `-Wlogical-op`, `-Wjump-misses-init`, `-Wtrampolines`,
  `-Wduplicated-branches` or `-Warith-conversion`; under `-Werror` an unknown `-W`
  name is an error, so that config adds `-Wno-unknown-warning-option`. clang checks
  the rest, and found two things gcc does not report: `qwe_vfmt` passes a format
  string on without `format(printf, 3, 0)`, and `luacbor.c` passed a `float` to
  `lua_pushnumber` without saying so.

No `#pragma GCC diagnostic` and no per-file `-Wno-` silences a finding. A finding is
fixed in the code. (The one per-target `-Wno-` in `src/`, `valgrind_smoke_test`'s
`-Wno-maybe-uninitialized`, predates this: that test's uninitialised read is the
point, and it is built only under `--config=valgrind`.)

## At -O2

`-Wnull-dereference`, `-Wstrict-overflow`, `-Wmaybe-uninitialized` and parts of
`-Wformat-overflow`, `-Wformat-truncation` and `-Wstringop-*` depend on the
optimisation passes, and are close to silent at the default fastbuild `-O0`. A zero
at `-O0` is no evidence for them. `--config=warnings-o2` adds `--copt=-O2` and
nothing else. It builds, it does not ship: the release build stays at fastbuild
(`.bazelrc`, `build:release`). LuaJIT keeps its `-O0` (its `BUILD` copts come
after `--copt`).

CI: the `compiler-warnings` job in `static-analysis.yml`, a pull request gate
(`docs/ci-checks.md`).

## Measuring

Bazel prints a warning only when it runs the compile action, and a cached action
is not re-run, so a clean-looking build proves nothing about warnings. To see every
warning of a tree, change the command line of every action, and keep going past
`-Werror`:

```
bazel build --keep_going --copt=-Wno-error --host_copt=-Wno-error \
    --copt=-DQWE_WARN_PROBE=$RANDOM //...
```
