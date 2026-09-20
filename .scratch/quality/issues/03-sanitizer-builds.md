# 03: Sanitizer builds for ASan, UBSan and MSan

Status: ready-for-agent
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

- [ ] `bazel test --config=ubsan //...` builds and runs the whole suite, with `-fno-sanitize-recover` so a finding fails the test rather than printing and continuing.
- [ ] `bazel test --config=asan //...` does the same, and sanitizer reports from forked children do not reach the e2e goldens. `e2e: tests/e2e/tracer_hello/` (existing, must pass unchanged under the config)
- [ ] The known UBSan finding is reproduced by the config before `m1-review/02` is fixed, and gone after. `manual: run --config=ubsan against tests/e2e/validate_timeout_too_large before and after`
- [ ] A deliberately faulty build fails under each config, so the configs are known to be live rather than silently disabled. `unit: src/kernel/sanitizer_smoke_test.c` (a test that is expected to trip the sanitizer, tagged so it runs only under these configs)
- [ ] MSan is either working over the vendored dependencies, or a decision is recorded in this ticket's comments explaining what blocks it and what covers the gap instead.
- [ ] `strerrorname_np` has a fallback for a libc that lacks it, or the minimum glibc version is stated in the build documentation.
- [ ] Which configs run in CI, and how often, is written down: the full matrix on every change is slow, and a nightly UBSan run plus per-change ASan may be the right trade.

## Comments
