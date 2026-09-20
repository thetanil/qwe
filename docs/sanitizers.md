# Sanitizer builds

```
bazel test --config=ubsan //...     # undefined behaviour, no recovery
bazel test --config=asan  //...     # memory errors and leaks, no recovery
```

A finding fails the test (`-fno-sanitize-recover=all`). Each config also builds
`//src/kernel:sanitizer_smoke_<config>_test`, which faults on purpose and
passes only if the sanitizer stops it; it is skipped by a plain
`bazel test //...`, so a config that silently lost its flags cannot pass.

## UBSan

`-fsanitize=undefined,float-cast-overflow`. `float-cast-overflow` is not part of
`undefined` on gcc or clang, and it is the check that finds a `double` too large
for a `long`. Vendored code (`third_party/`) is built without any sanitizer:
LuaJIT shifts into the sign bit on purpose (`lj_buf.c`), and that is upstream's
to keep or fix. Code under `src/`, `plugins/` and `tools/` is checked in full.

## ASan

Includes LeakSanitizer. `tests/e2e/run_case.sh` sends reports to per-pid files
(`asan-<case>.<pid>`) in the test's undeclared outputs, so a report from a
forked step child never reaches a golden. Two cases need help, and the harness
handles both:

- `ulimit` cases run with `detect_leaks=0`: a process limit starves LeakSanitizer
  of the thread it needs and it dies with a fatal error of its own.
- a case may carry an `asan-options` file (extra `ASAN_OPTIONS`).
  `plugin_segfault_isolated` sets `handle_segv=0`, because the crash is the
  point and ASan would otherwise turn the SIGSEGV into an exit.

## MSan: not supported

Decided with evidence; do not revisit without a new reason.

- LuaJIT instrumented: the first `lua_newstate` reports a use of an uninitialised
  value in `lj_prng_condition`, because the seed is written by code MSan cannot
  see. Every test dies there.
- LuaJIT left uninstrumented (its interpreter is hand-written assembly, its JIT
  emits machine code): its writes never clear MSan's shadow, so instrumented
  code that reads them reports false positives (`lptree.c:471`, `luacbor.c:256`).

MSan needs every linked object instrumented, and LuaJIT cannot be. Coverage of
what MSan would find comes from ASan, UBSan and the valgrind gate
(`quality/04`).

## libc

`src/kernel/trace.c` uses `strerrorname_np` only on glibc 2.32 or later.
Elsewhere (musl, older glibc) it uses a table of the common errnos, and any
other errno prints as `E<number>`. `errno_name_test` builds `trace.c` with
`strerrorname_np` made unavailable, to keep the fallback compiling and correct.

## CI cadence

| When | What | Why |
|---|---|---|
| every change | `bazel test //...` and `--config=asan` | ASan is the cheapest way to catch a memory error before it merges |
| nightly | `--config=ubsan` | fast, but a finding is rarely urgent enough to block a change |

There is no CI service configured in this repo yet; this is the intended
schedule for when there is one.
