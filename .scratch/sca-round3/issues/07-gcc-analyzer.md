# 07: GCC's analyzer

Status: resolved
Category: bug
Type: task

## What

`gcc -fanalyzer` (GCC 13.3 here, Ubuntu 24.04) is a second, independent static
analyzer, and the toolchain is already GCC. On this tree, without `-Werror`
(`bazel build --keep_going --copt=-Wno-error --copt=-fanalyzer //src/... //tools/...`)
it reports, in `src/` and `tools/` (not `third_party/`):

- 13 `-Wanalyzer-fd-leak`: `luaexec.c:47` (9: `in_p`, `out_p`, `err_p` and their
  `[status]` variants) and `oom_shim.c:207` (4: `cntp`, `errp`);
- 2 `-Wanalyzer-malloc-leak`: `encrypt.c:89` (`plain`), `jobs.c:122`
  (`strdup(lua_tolstring(...))`);
- 2 `-Wanalyzer-double-free`: `luaexec.c:335` (`values`) and `:339` (`names`);
- 1 `-Wanalyzer-null-argument`: `luaexec.c:38` (`memcpy` in `buf_append`).

clang-tidy reports none of them. Triage so far, from reading and one valgrind
run: the two `luaexec.c` double-frees (a `realloc` failing on one of two arrays)
and the `encrypt.c` leak (`free(plain)` runs on every path I read) look like
GCC 13's realloc and loop modelling, not bugs. The fd paths, `jobs.c:122` and
`luaexec.c:38` were **not** triaged. Ticket 02 came out of reading around this
output, so read each one.

## Approach

For each of the 18: reproduce or refute. A true positive is fixed, with a test if
it is an error path. A false positive is removed by changing the code so the
analyzer's path is not possible (an early `return`, splitting the function, an
explicit initialisation of the array `pipe2` fills, `errno` handling that
closes the descriptors on the branch the analyzer follows); a code shape that
is easier for an analyzer to follow is easier for a reviewer too. Do not add a
`#pragma GCC diagnostic` or a `NOLINT` here: an exception costs more than the
rewrite.

Then decide about gating: with zero findings, add `-fanalyzer` to a CI job
(the GCC 13 analyzer is slow; measure it, and use a `--config=analyzer` in
`.bazelrc`) so it keeps being zero. Ticket 04 pins clang-tidy, not GCC: the
analyzer's findings change between GCC releases, so the job must also pin its
GCC (the runner image's `gcc-13` package version, recorded, or a container by
digest) and print `gcc --version` into the same kind of evidence artifact as
ticket 04. If
some findings cannot be removed by a rewrite, do not adopt it, list them in the
Comments as GCC-13 limitations with the gcc bug number if there is one, and
propose GCC 14 (whose analyzer handles C considerably better) as the follow-up.

## Acceptance criteria

- [x] Each of the 18 findings has a row in the Comments: true or false
      positive, the evidence, and the change. `manual: Comments`
- [x] Every true positive has a test that fails before the change.
      `unit: <the tests>`
- [x] `bazel build --copt=-fanalyzer //src/... //tools/...` reports zero
      `-Wanalyzer-*` in `src/` and `tools/`, or the ticket says why not and
      leaves GCC-14 as the next step. `manual: command`
- [x] If zero: a CI job or config keeps it zero.
      `manual: .bazelrc config and workflow`
- [x] `tools/clang-tidy/run.sh` exit 0, `bazel test //...` and the coverage
      check are green.

## Comments

Worked on `sca-round3/07-gcc-analyzer`, from `origin/main` at `47edde6`, with gcc-13
`13.3.0-6ubuntu2~24.04.1`. The ticket's command
(`bazel build --keep_going --copt=-Wno-error --copt=-fanalyzer //src/... //tools/...`)
reproduced **16** of the 18. The two `luaexec.c` double-frees are gone: ticket 02
rewrote `exec_preamble` (`names`/`values` are two `malloc`s checked together, with no
`realloc` left). Line numbers below are from that run (`jobs.c:134` was `:122` when
the ticket was written, `luaexec.c:40` was `:38`).

The analyzer's view of descriptors was established with a probe
(`pipe2`, then `if (fd >= 0) close(fd)`, three lines): GCC 13 marks a descriptor
`pipe2` returned as open **without constraining its value to `>= 0`**, so the false
side of any `>= 0` or `!= -1` test on it is, to the analyzer, a leak. An explicit
`if (p[0] < 0) return -1;` after `pipe2` makes it worse (it reports the leak at the
`return`). Only an unconditional `close`, or an open-state flag that is not the fd's
own value, passes. I found no GCC bug number for this.

### The findings

| # | Where | Finding | Verdict | Evidence | Change |
|---|---|---|---|---|---|
| 1–2 | `oom_shim.c:207` `errp[0]`, `errp[1]` | fd-leak | **true** | `if (pipe(errp) < 0 \|\| pipe(cntp) < 0) return -1;`: when the first `pipe` succeeds and the second fails, `errp` is never closed. New `probe_closes_the_first_pipe_when_the_second_fails` (`RLIMIT_NOFILE` leaves room for one pipe) failed before the change: `the first pipe's read end is closed`. | staged unwinding: `goto close_errp` / `close_cntp` |
| 3–4 | `oom_shim.c:207` `cntp[0]`, `cntp[1]` (and `errp` again) | fd-leak | **true** | `if (pid < 0) return -1;` after `fork` fails leaks all four ends. New `probe_closes_its_pipes_when_fork_fails` (`RLIMIT_NPROC` = 0; skipped as root, where the limit does not bind) failed before the change. | as above |
| 5–7 | `luaexec.c:49` `in_p[0]`, `in_p[1]`, `in_p[status]` | fd-leak | false | Paths: `pipe2(in_p)` succeeds, then `close_fd`'s `if (*fd >= 0)` takes its false branch. `pipe2` does not write the array on failure, so every `-1` sentinel was correct. It is the probe's behaviour above (`[status]` is the analyzer mislabelling an element). | `struct end { fd, open }` for the parent's ends; `open_pipes` unwinds in stages; the child's ends and the `fork`-failure path close unconditionally |
| 8–10 | `luaexec.c:49` `out_p[0]`, `out_p[1]`, `out_p[status]` | fd-leak | false | same | same |
| 11–13 | `luaexec.c:49` `err_p[0]`, `err_p[1]`, `err_p[status]` | fd-leak | false | same | same |
| 14 | `luaexec.c:40` `buf_add` | null-argument (`memcpy` into NULL) | false | Path: first `read` into an empty `struct buf`, analyzer takes `len + n > cap` false with `data == NULL`. `cap == 0` and `n == got > 0` make that branch impossible. | the condition says `!b->data \|\| n > b->cap - b->len` |
| 15 | `jobs.c:134` `strdup(s)` | malloc-leak | false | The leak is reported on re-reading `j->needs` right after storing `j->needs[k]`: `j` is `jobs[i]`, both are symbolic offsets, and the store drops the analyzer's binding for `jobs[i].needs`. `qwe_jobs_free` frees `needs[k]` for every `k < nneeds`, and `j->needs` owns the array before the first `strdup`. Moving the `strdup` into a local alone did **not** clear it (the proof it is the array binding, not the call). | the array is filled through a local `char **needs` |
| 16 | `encrypt.c:89` `plain` | malloc-leak | false | Path: `read_stdin` returns with `got == 0`, then the caller takes `n < 0`. The analyzer loses `n <= MAX_PLAIN` across the loop, so `(ssize_t)n` looks possibly negative with `*out` set. `free(plain)` runs on every real path. | `read_stdin` returns 0/-1/-2 and writes the length through `size_t *len`, so no path returns a failure with the buffer handed out |
| 17–18 | `luaexec.c:335`/`:339` `values`, `names` | double-free | not reproduced | Gone at `47edde6` (ticket 02's rewrite). | none |

A trap along the way: the first version of `open_pipes(int in_p[2], int out_p[2], int err_p[2])`
passed a single-file run and then failed under bazel (`leak of file descriptor` at
the second `pipe2`). Analyzed on its own, the three array parameters may alias, so a
`pipe2` into one may overwrite the other's descriptors. The pipes are one
`struct pipes` now. The analyzer's result also depends on its exploration budget, so
the whole-tree bazel run is the check, not a single file.

### Gating: adopted

- `.bazelrc`: `build:analyzer --per_file_copt=^src/.*,^tools/.*@-fanalyzer`.
  `third_party/` is excluded. It compiles with `-w` anyway, and LuaJIT is most of
  the analyzer's cost.
- Cost, measured with fresh command lines (`--disk_cache=` and a unique `-D`):
  **4.1 s** for all 86 sources at `--jobs=4` (a runner's size), against **0.6 s**
  without `-fanalyzer`. At 16 jobs it is 3.1 s against 0.3 s. That is cheap enough to
  gate every pull request, not only nightly.
- `tools/gcc-analyzer/run.sh [--exact] [--evidence-dir DIR]`:
  - checks the GCC major against `tools/gcc-analyzer/pin.env`, and with `--exact`
    (CI) the exact package, `gcc-13` `13.3.0-6ubuntu2~24.04.1`;
  - lists the sources bazel really compiles with `-fanalyzer` (`aquery`) and fails if
    any `.c` under `src/`, `tools/` or `plugins/` is missing, beyond four named
    exclusions: the sanitizer and valgrind smoke tests (deliberate faults, built only
    under their configs) and the two libFuzzer harnesses (clang only);
  - runs the build and writes the evidence: `version.txt`, `pin.env`, `bazelrc.txt`,
    `files.txt`, `excluded.txt`, `sha.txt`, `output.txt`, `exit_status.txt`.
- The pin is recorded, not installed: the runner image ships `gcc-13`, and the Ubuntu
  archive keeps only the latest `-updates` build, so `apt install gcc-13=<v>` would
  break rather than hold. `--exact` fails instead when the image moves, which is the
  prompt to re-triage and re-pin.
- CI: a `gcc-analyzer` job in `static-analysis.yml`, so it runs on push, pull
  request, nightly and release (both already call that workflow) with no new badge.
  It uploads `gcc-analyzer-evidence-<sha>`, and `release.yml` attaches it as
  `gcc-analyzer-evidence-<version>.zip`. `docs/ci-checks.md` lists it in the table,
  in "Every push" and in the pull request gates (now eight names for the ruleset).
  `docs/static-analysis.md` has a "GCC's analyzer" section.
- The gate goes red: with `oom_shim.c` put back to `origin/main`, `run.sh` exits 1
  with the four `-Werror=analyzer-fd-leak` errors.

GCC 14 is not needed to reach zero. It remains the natural next step for a
stronger analyzer: a new pin plus a re-triage, and a toolchain change for the
whole build, so it is a ticket of its own.

### Checks

- `tools/gcc-analyzer/run.sh --exact`: exit 0, `no -Wanalyzer findings`, 86 sources,
  4 excluded. The ticket's command with `-Werror` kept
  (`bazel build --keep_going --copt=-fanalyzer //src/... //tools/...`): builds, zero
  `-Wanalyzer-*` in `src/` or `tools/`.
- `bazel test //...`: 265 passed, 3 skipped (config-gated). `oom_shim_test`,
  `luaexec_test`, `jobs_test` and `//src/cli/encrypt:oom_test` also pass under
  `--config=asan`, `ubsan` and `valgrind`. `oom_shim_test` runs 4, skips 0.
- `tools/clang-tidy/run.sh`: exit 0 (186 pairs, CTU on).
- `bazel run //tools/coverage:check`: no touched file is below 85%. Locally it fails on
  `plugins/builtin/backend-ssh/ssh.lua` (84.7%) and `src/kernel/lua/become.lua`
  (81.2%), because the devcontainer's ssh target fails host-key verification, so
  the ssh e2e cases skip. Neither file is touched here, and CI's coverage job runs
  its own sshd.
- `//tools/ci:workflows_test` passes with the new job and command.
