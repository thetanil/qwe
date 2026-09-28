# Static analysis, round 3: fix, do not justify

Status: ready-for-agent

## Premise

qwe is a software tool under ISO 26262 (ISO 26262-8 clause 11), used for items
up to ASIL D (`docs/adr/0015-qwe-is-qualified-for-tcl3-asil-d-by-validation.md`, draft).
That is why code quality is held to a standard an assessor would accept. Every
exception (a `-check` line in `.clang-tidy`, a `CheckOptions` narrowing, a
`NOLINT`, a "wontfix" row in `docs/static-analysis.md`, a `(void)` cast that
says "ignored on purpose") costs three things: someone writes the
justification, the assessor reads and challenges it, and it has to be kept true
as the code changes. A code fix costs once and the
assessor never sees it. So in this round **the default answer to a finding is to
change the code, and an exception has to beat that on cost, not on convenience.**

Rounds 1 and 2 (`.scratch/archive/sca-findings`, `.scratch/archive/sca-exclusions`) took
the opposite default in a few places, and closed them with reasons an assessor
would push back on. Round 2 ended with 3 excluded checks, 2 narrowed ones, 4
`NOLINT`s, and a doc full of reasoning. This round removes every one of those
that a code change can remove, leaves the rest as a short, visible, ticketed
register (expected: one `NOLINT` and two directory-scoped name lists), and adds the checks and tools that round 1 and 2 did not look at.

## What round 2 got wrong (measured on 2026-09-26, HEAD `bb9d69f`)

| Round 2 said | What it rested on | Why it does not survive |
|---|---|---|
| `multi-level-implicit-pointer-conversion` stays excluded: "the only way to quiet it is a cast at each of the 50" | An assumption | False: typed helpers put the (explicit) conversion in one place. The exclusion also drops the check's real target, `free(&p)` and `qsort(&arr, ...)`. Ticket 10. |
| `easily-swappable-parameters` stays excluded: "a class of mistake this codebase has not made" | The check was off the whole time, so it could not have found one: circular | 30 findings; `send_result(L, idx, fd)` (two `int`s), `qwe_summary_write(path, run_dir, workflow_file, ...)` and `ssh_call(fn, a, b)` compile when swapped and are wrong. Tickets 11, 12. |
| `concurrency-mt-unsafe` narrowed to `FunctionSet: glibc` | "qwe has no threads", tested by a symbol check on one binary | A narrowing is an exception. Under the default set there are 25 findings, all `getenv`/`readdir`. All but one go: `getenv` moves into one `qwe_env_load`, which keeps one visible, justified `NOLINT` (not an `environ` walk that hides the same call from the check). Tickets 17, 18. |
| Reserved identifiers allowed by name (5 names, 76 sites) | "required by the language / the linker" | The feature-test macros belong in build flags, not source. `__executable_start` has an ordinary replacement. Only `__wrap_*`/`__real_*` (the linker) and `__gcov_dump`/`__gcov_reset` (libgcov; in `gcov.h`, never linted because the coverage build is not) are mandated, and those can live in two small directories with their own config. Tickets 05, 13-15. |
| 4 `NOLINT`s | "single proven false positive" | One is a real code smell (`workflow.c:220`), three ride on the no-threads premise. Tickets 18, 19. |
| The `qwe_out_*` helpers: "the `ferror` check follows" | A comment | Nothing enforces it. A writer that forgets the check passes every gate. Ticket 20. |
| Nothing said about recursion or complexity | Not looked at | 5 recursive functions, 13 above cognitive complexity 25. Tickets 21, 22. |

## What round 1 and 2 never looked at

Measured this session, and what it found:

- **Lua-to-C error paths leak.** `qwe.exec.preamble({A = {}})` and
  `qwe.exec.run({"true", {}})` leak (valgrind: definitely lost): the C function
  allocates, then calls `luaL_checkstring`, which longjmps. clang-tidy cannot see
  it, and no test sends a wrong-typed value across the boundary. Tickets 2, 3.
- **GCC's analyzer** (`-fanalyzer`, GCC 13.3) reports 13 fd-leak, 2 malloc-leak,
  2 double-free and 1 null-argument warning that clang-tidy does not. Some are
  false positives (the two `realloc` "double-frees" and the `encrypt.c` leak look
  like GCC 13's realloc/loop modelling); the fd paths and `jobs.c:122` are
  untriaged. Ticket 7.
- **The compiler is under-used.** With extra warnings on: `-Wwrite-strings` 78,
  `-Wswitch-enum` 88, `-Wmissing-prototypes` 21, `-Wcast-qual` 10,
  `-Wsign-conversion` 5, and singles (`-Wbad-function-cast`, `-Wfloat-equal`,
  `-Wswitch-default`, `-Wformat`). Zero for `-Wshadow`, `-Wconversion`,
  `-Wformat=2`, `-Wnull-dereference`, `-Wfree-nonheap-object` (the Bazel toolchain
  turns the last one off). Tickets 8, 9.
- **CI is not the tool the tickets measured.** CI installs `apt: clang-tidy`,
  which on ubuntu-24.04 is clang-tidy 18. Every count in rounds 1 and 2 is from
  clang-tidy 20. The gate runs only nightly, on release and by hand, never on a
  push or pull request (`tests.yml` has no `pull_request` trigger either), and
  keeps no evidence. Ticket 4.
- **The gate lints less than it says.** A file built more than once is linted
  once, against its first compile action: for `trace.c` that is the test build
  with `QWE_NO_STRERRORNAME_NP`, so the production configuration is never
  linted. The two `manual` fuzz binaries are never linted, and neither is any `.bazelrc` configuration but the default: `gcov.h`'s `QWE_GCOV` branch (`bazel coverage`) has never been linted. Ticket 5.
- **The clang analyzer runs one translation unit at a time**, with POSIX
  modelling at its default. Ticket 6.
- **The kernel's own Lua is not linted.** luacheck (vendored) runs only on
  built-in plugins and on project plugins at `qwe validate`. `src/kernel/lua/*.lua`,
  the `*_test.lua` files and the plugin `test.lua` files get no analysis.
  Measured: 7 findings, 5 of them in the `compat53.lua` polyfill. Tickets 23-25.
- **CodeQL runs almost unconfigured**: `build-mode: none` for C, default query
  suite, not a gate, and not mentioned in `docs/static-analysis.md`. Ticket 26.
- **Vendored code is out of scope for our analysis and in scope for the
  certification.** libsodium, libyaml, LuaJIT, TinyCBOR, PCRE2, LPeg, lrexlib,
  dkjson ship in the binary. As part of the tool, they are covered by its qualification, and 1c validation needs their versions and known anomalies. Ticket 27.

## The standard, and why this feature does not wait for it

ISO 26262, qwe as a software tool, items up to ASIL D. The classification and
the qualification work live in their own feature,
`.scratch/iso26262-tool-qualification`, and in ADR-0015 (draft). That ADR qualifies
the kernel and the built-in plugins for TCL3 at ASIL D by validation, which tests
behaviour and does not gate on this feature. This feature's results are cited
there as supporting evidence. This feature goes ahead because its findings are
real defects, and fewer defects in the path from exit status to outcome mean
fewer false passes. **No ticket here is blocked by that feature, and this
feature closes on its own tickets.** Tickets 01 and 30 moved there on 2026-09-26
(as its 01, and as a fallback ticket since replaced by its 07); the numbers are
left as gaps here so that the cross-references stay valid.

## Not ticketed, and what would change that

Three idioms stay, because there is no code change that removes the situation
and each is a well-known form:

- `qwe_diag` (`(void)vfprintf(stderr, ...)`): a failed write to `stderr` has no
  one to report to.
- `qwe_msg` (`(void)vsnprintf`): a cut diagnostic message is only shorter.
- `(void)fclose(fp)` on a stream opened read-only.

If the ISO 26262 feature ever falls back to 1d (ADR-0015 rejects it), and its
subset does not accept a `(void)`-cast discarded return, these become tickets
there.

## Order

1. **02**, **03**: the bug, and the class it belongs to.
2. **04**, **05**, **06**, **07**, **08**, **09**: make the gate and the
   compiler tell the truth. Every later ticket is measured with them, and
   ticket 08 and 09 add findings that must be fixed too.
3. **10**-**22**: remove each remaining exception by changing the code.
   **10** before **11** (it deletes `key_cmp`); **13**, **14** and **05** before
   **15**; **17** before **18** (18's tests use 17's `struct qwe_env`); **21**
   before **22**.
4. **23**-**25**: Lua.
5. **26**, **27**: CodeQL, vendored code.
6. **28**: enforce "no exceptions" by a test, so it stays true. **29**: rewrite
   the doc as a register of what is left (ideally nothing), rerun everything,
   and archive.

## Constraints

- No Python (`CLAUDE.md`). A tool that needs Python to run is not adopted.
- `third_party/` is not edited (ticket 27 records it, it does not change it).
- A fix is a code change. A `NOLINT`, an excluded check, a `CheckOptions`
  narrowing or a new `wontfix` needs its own ticket comment saying why a fix
  costs more than the exception, and the assessor cost is part of "costs".
- Each ticket ends with `tools/clang-tidy/run.sh` at exit 0, `bazel test //...`
  green, and `bazel run //tools/coverage:check` green (85% per file).
  A new error branch gets a test that reaches it.
- One branch, one commit and one PR per closed ticket, never push to `main`
  (`CLAUDE.md`, project rules). Ticket 26 needs a CI run on GitHub to finish: the
  agent pushes its branch, opens the PR and reads the run itself; only a step
  that needs a release, a tag or a repo setting is left as `Status: ready-for-human`
  with the remaining steps. (Ticket 04 was closed under the earlier never-push rule.)
- Silencing a check without a marker is worse than a `NOLINT`: do not rewrite
  code into a form the check happens not to model (walking `environ` instead of
  `getenv`, a cast chain that hides a conversion) to make a finding go away. The
  fix changes what the code does or how clearly it says it; otherwise it is a
  visible, ticketed exception.
- An option that makes a tool see **more** (an analyzer mode on, a lower
  threshold) is configuration, recorded as `widen` in ticket 28's
  `tools/ci/exceptions.txt`. Only options that make it see less are exceptions.

## Tickets

- 01: moved to `.scratch/iso26262-tool-qualification` (its 01)
- [02: `luaexec.c` leaks on a Lua error](issues/02-luaexec-longjmp-leaks.md)
- [03: every C function Lua can call, with wrong arguments](issues/03-lua-boundary-audit.md)
- [04: pin clang-tidy, run it on every push and PR, keep evidence](issues/04-ci-pin-run-evidence.md)
- [05: lint every compile configuration and the fuzz harness](issues/05-lint-every-config.md)
- [06: the clang analyzer, deeper](issues/06-analyzer-depth.md)
- [07: GCC's analyzer](issues/07-gcc-analyzer.md)
- [08: compiler warnings the toolchain leaves off](issues/08-compiler-warnings.md)
- [09: `-Wswitch-enum`](issues/09-switch-enum.md)
- [10: typed pointer-array helpers, and `multi-level-implicit-pointer-conversion` on](issues/10-multi-level-pointer-helpers.md)
- [11: swappable parameters in `src/` and `tools/`](issues/11-swappable-src.md)
- [12: swappable parameters in tests](issues/12-swappable-tests.md)
- [13: feature-test macros into the build flags](issues/13-feature-test-macros.md)
- [14: `__executable_start`](issues/14-executable-start.md)
- [15: toolchain-mandated reserved names (`__wrap_*`, `__real_*`, `__gcov_*`) confined to two directories](issues/15-wrap-shim-confinement.md)
- [16: `cert-dcl51-cpp`](issues/16-dcl51-exclusion.md)
- [17: `concurrency-mt-unsafe` at its default set](issues/17-mt-unsafe-any.md)
- [18: the no-threads `NOLINT`s](issues/18-mt-unsafe-nolints.md)
- [19: the `unix.Stream` `NOLINT`](issues/19-nolint-stream.md)
- [20: a writer type that carries its own error](issues/20-writer-type.md)
- [21: recursion](issues/21-recursion.md)
- [22: cognitive complexity](issues/22-complexity.md)
- [23: luacheck over all of the repo's Lua](issues/23-luacheck-all-lua.md)
- [24: luacheck, strict](issues/24-luacheck-strict.md)
- [25: a second Lua analyzer, evaluated](issues/25-lua-analyzer-spike.md)
- [26: CodeQL on a real build](issues/26-codeql.md)
- [27: a register of the vendored components, for tool qualification](issues/27-third-party-register.md)
- [28: "no exceptions" enforced by a test](issues/28-no-exceptions-test.md)
- [29: closeout: the register, and everything rerun](issues/29-closeout.md)
- [31: `summary.c`'s `log_tail` sizes a file, then reads it](issues/31-log-tail-size-then-read.md)
- 30: moved to `.scratch/iso26262-tool-qualification`, since replaced by its 07
