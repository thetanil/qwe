# Static analysis findings: the bug-finding classes

Status: ready-for-agent

## What

The static-analysis gate (`docs/static-analysis.md`, archived feature
`static-analysis`) exits 0, but only because 26 checks are excluded in
`.clang-tidy` and five more are narrowed out of `*_test.c` by
`tools/clang-tidy/run.sh`. The "N warnings generated" lines the gate prints
(about 1,000 of them across 78 files) are those excluded findings plus system
headers. `--quiet` hides that breakdown, so the gate looks noisy but reports nothing.

Measured on 2026-09-23 against `acb416d`: the raw ruleset (every group on, no
exclusions, no test narrowing) gives **619 findings**, and **none of them are in a header**.

This feature re-enables the exclusions most likely to be hiding real bugs, one
class per ticket. Each ticket fixes every finding in its class and removes that
class's row from the exclusion table. The rest of the exclusions, the
style and noise classes, are out of scope (below) and become the next feature.

## Classes in scope

| Ticket | Check(s) re-enabled | Findings | Why it is a bug class |
|---|---|---|---|
| 01 | gate itself | n/a | `HeaderFilterRegex` never matches (see ticket); the backlog is invisible |
| 02 | `clang-analyzer-core.StackAddressEscape`, `clang-analyzer-core.CallAndMessage` | 24 + 1 | `lifecycle_test.c` hands greatest a stack buffer that is read after the test returns, so a failure prints garbage |
| 03 | test-only narrowing in `run.sh` (`unix.Malloc`, `unix.Stream`, `core.NonNullParamChecker`, `unix.StdCLibraryFunctions`, `optin.portability.UnixAPI`) | 30 | leaks, null args, unclosed streams in tests |
| 04 | `cert-err33-c`, narrowed to resource and syscall returns | 62 | `fclose`/`fflush`/`fwrite`/`fseek` failures lose data; `clock_gettime`/`timerfd_settime`/`gmtime_r`/`strftime`/`signal` failures go unnoticed |
| 05 | `cert-err33-c`, extended to `snprintf`/`sprintf` | 96 | silent truncation; `sprintf` has no bound at all |
| 06 | `cert-err34-c`, `clang-analyzer-unix.Errno` | 6 + 3 | `atol`/`sscanf` cannot report a bad parse; errno read after being clobbered |
| 07 | `clang-analyzer-security.insecureAPI.strcpy`, `clang-analyzer-optin.taint.TaintedAlloc` | 3 + 1 | unbounded copies; allocation sized from unvalidated input |

## Out of scope: the next feature

Every remaining exclusion stays as it is for now, but each still needs its
"disabled for reasons" justification rechecked in a follow-up feature:

| Check(s) | Findings |
|---|---|
| `cert-err33-c` on `fprintf`/`fputs`/`fputc` | 166 |
| `bugprone-reserved-identifier`, `cert-dcl37-c`, `cert-dcl51-cpp` | 68 |
| `concurrency-mt-unsafe` (`strerror` 19, `getenv` 14, `sigprocmask` 12, `rand` 5, `readdir` 4, other 3) | 57 |
| `bugprone-multi-level-implicit-pointer-conversion` | 50 |
| `bugprone-easily-swappable-parameters` | 28 |
| `bugprone-assignment-in-if-condition` | 9 |
| `bugprone-implicit-widening-of-multiplication-result`, `bugprone-misplaced-widening-cast` | 8 |
| `cert-msc30-c`/`msc32-c`/`msc50-cpp`/`msc51-cpp` (`rand`) | 6 |
| `bugprone-unsafe-functions`, `cert-msc24-c`/`msc33-c` (`rewind`) | 3 |
| `clang-analyzer-optin.performance.Padding` | 1 |

## Constraints

- No Python (`CLAUDE.md`).
- `third_party/` is vendored and is not edited. A finding that traces into
  `greatest.h` gets fixed at the call site in our test, not in the header.
- A fix is a code change. A `NOLINTNEXTLINE(<check>)` is allowed only for a
  single proven false positive, with a comment saying why, which is the rule
  `docs/static-analysis.md` already sets. If a ticket finds a whole class is
  false positives after all, it stays excluded, and the ticket is closed
  `wontfix` with the evidence in its comments.
- Each ticket ends with `tools/clang-tidy/run.sh` at exit 0 with its checks
  enabled, `bazel test //...` green, and the check's row gone from
  `docs/static-analysis.md` with the real bugs found added under "What the gate found".

## Tickets

- [01: fix the gate's header filter and make the excluded backlog visible](issues/01-gate-header-filter-and-tally.md)
- [02: dangling greatest message in lifecycle_test](issues/02-greatest-dangling-message.md)
- [03: analyzer checks in test code](issues/03-analyzer-in-tests.md)
- [04: unchecked resource and syscall returns](issues/04-unchecked-resource-returns.md)
- [05: snprintf/sprintf truncation](issues/05-snprintf-truncation.md)
- [06: string-to-number conversion and errno](issues/06-conversion-and-errno.md)
- [07: unbounded strcpy and tainted allocation](issues/07-strcpy-and-tainted-alloc.md)
