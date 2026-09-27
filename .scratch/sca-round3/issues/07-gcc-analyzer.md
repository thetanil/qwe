# 07: GCC's analyzer

Status: ready-for-agent
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

- [ ] Each of the 18 findings has a row in the Comments: true or false
      positive, the evidence, and the change. `manual: Comments`
- [ ] Every true positive has a test that fails before the change.
      `unit: <the tests>`
- [ ] `bazel build --copt=-fanalyzer //src/... //tools/...` reports zero
      `-Wanalyzer-*` in `src/` and `tools/`, or the ticket says why not and
      leaves GCC-14 as the next step. `manual: command`
- [ ] If zero: a CI job or config keeps it zero.
      `manual: .bazelrc config and workflow`
- [ ] `tools/clang-tidy/run.sh` exit 0, `bazel test //...` and the coverage
      check are green.

## Comments
