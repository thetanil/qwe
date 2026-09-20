# 03: Sanitizer builds for ASan, UBSan and MSan

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

Nothing in the build turns on a sanitizer today. `.bazelrc` sets `-std=c99
-Wall -Wextra -Werror` and stops there. Three configs are worth having, and
they are not equally easy.

**UBSan first, because there is already something for it to find.**
`m1-review/02` records a `double`-to-`long` conversion that is undefined for a
large `timeout-minutes`. That is one known finding; the point of the config is
the ones nobody has found. It is also the cheapest to adopt: it needs no
special libc handling and almost no runtime cost.

**ASan next.** Straightforward for the C, with two complications specific to
this codebase:

- qwe forks constantly, and every step child runs with the sanitizer's runtime
  inherited. Output from a child's report interleaves with the step's own
  stdout on the same pipe. `log_path` in `ASAN_OPTIONS` sends reports to files
  named per pid, which keeps them out of the goldens.
- The e2e harness compares stdout and stderr against goldens byte for byte. Any
  sanitizer that writes to stderr will fail every case. The harness needs to
  either route reports to files or be taught to strip them — routing is
  cleaner, and it keeps the reports.

**MSan is the expensive one** and should be scheduled last, or dropped if it
does not earn its place. It needs every linked library instrumented, and qwe
vendors LuaJIT, libyaml, libsodium, TinyCBOR and LPeg. Uninstrumented code
produces false positives, so either all of it is rebuilt under MSan or MSan is
not used. LuaJIT in particular does its own stack and memory tricks and is
historically unhappy under MSan. Decide with evidence: try it, and if LuaJIT
makes it unusable, record that and rely on ASan and valgrind instead. That
decision belongs in this ticket's comments so it is not re-litigated.

**Also settle the libc question here**, since it is a build concern.
`src/kernel/trace.c` calls `strerrorname_np`, which is a GNU extension present
from glibc 2.32. If certification pins a platform, or if musl is ever a target,
that call needs a fallback. It is one function and the fallback already exists
in the same file for the unknown-errno case.

## Acceptance criteria

- [x] `bazel test --config=ubsan //...` builds and runs the whole suite, with `-fno-sanitize-recover` so a finding fails the test rather than printing and continuing.
- [x] `bazel test --config=asan //...` does the same, and sanitizer reports from forked children do not reach the e2e goldens. `e2e: tests/e2e/tracer_hello/` (existing, must pass unchanged under the config)
- [x] The known UBSan finding is reproduced by the config before `m1-review/02` is fixed, and gone after. `manual: run --config=ubsan against tests/e2e/validate_timeout_too_large before and after`
- [x] A deliberately faulty build fails under each config, so the configs are known to be live rather than silently disabled. `unit: src/kernel/sanitizer_smoke_test.c` (a test that is expected to trip the sanitizer, tagged so it runs only under these configs)
- [x] MSan is either working over the vendored dependencies, or a decision is recorded in this ticket's comments explaining what blocks it and what covers the gap instead.
- [x] `strerrorname_np` has a fallback for a libc that lacks it, or the minimum glibc version is stated in the build documentation.
- [x] Which configs run in CI, and how often, is written down: the full matrix on every change is slow, and a nightly UBSan run plus per-change ASan may be the right trade.

## Comments

### Resolved

Configs are in `.bazelrc`; the write-up (flags, harness handling, MSan decision, libc, CI cadence) is `docs/sanitizers.md`. `bazel test //...`, `--config=ubsan` and `--config=asan` all pass.

**Findings, all in our code, all fixed** (the config found them on first run):
- `validate.c`, `luafs.c`: `qsort(NULL, 0, ...)` on an empty array (every valid workflow hit the first; every `fs.list` of an empty directory the second).
- `redact.c`: `memcpy` from a NULL `held` with length 0, on every `qwe_redact_feed` of a first chunk.

**m1-review/02 reproduced.** Built `7c7fbe5~1` with `--config=ubsan` and `timeout-minutes: 1e30`: `jobs.c:16:35: runtime error: 6e+34 is outside the range of representable values of type 'long int'`. The fixed tree is clean. Worth knowing: `-fsanitize=undefined` does **not** include `float-cast-overflow` on gcc or clang, so the first version of the config missed this one; it is now listed explicitly.

**Vendored code is not built with UBSan.** LuaJIT shifts into the sign bit deliberately (`lj_buf.c:298`), which fails every test under `-fno-sanitize-recover`. `third_party/` is compiled with `-fno-sanitize=all` under `--config=ubsan` (not under ASan).

**ASan / e2e harness.** `run_case.sh` routes reports to per-pid files in the test's undeclared outputs. `engine_error_spawn` (a `ulimit -u` case) killed LeakSanitizer, so ulimit cases run with `detect_leaks=0`. `plugin_segfault_isolated` needs `handle_segv=0` through the new optional `asan-options` case file. `--wrap` tests (`alloc_test`, `load_oom_test`, `jobs_test`, `workflow_test`) pass under ASan unchanged.

**Liveness.** `sanitizer_smoke_ubsan_test` and `sanitizer_smoke_asan_test` fault in a child and pass only if it dies; they are incompatible (skipped) outside their config. Seen failing before the config existed.

**MSan: dropped.** Tried with clang. Instrumented LuaJIT reports a use of uninitialised value in `lj_prng_condition` (`lj_prng.c:66`) on the first `lua_newstate`, so every test dies. With LuaJIT uninstrumented, its asm interpreter and JIT never clear the shadow and instrumented code reading its output (`lptree.c:471`, `luacbor.c:256`) reports false positives. Gap covered by ASan, UBSan and the valgrind gate (04). Not to be re-litigated without a new reason.

**libc.** `trace.c` uses `strerrorname_np` only on glibc >= 2.32; otherwise a table of common errnos, then `E<n>`. `errno_name_test` builds it with the function made unavailable.

**CI.** Per change: `bazel test //...` plus `--config=asan`; nightly: `--config=ubsan`. No CI service exists in the repo yet.

Note for 02: it asks for ASan *and MSan* runs; MSan is dropped, and the ASan run above already covers its files and `--wrap` tests.
