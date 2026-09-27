# 17: `concurrency-mt-unsafe` at its default set

Status: ready-for-agent
Category: enhancement
Type: task

## What

`concurrency-mt-unsafe` runs with `FunctionSet: glibc`, narrowing what it reports
to what glibc documents as thread-unsafe. A narrowing is an exception with a
justification ("glibc-safe as long as nothing calls `setenv`") that an assessor
has to accept. The default set (`any`) reports 25 more findings, all in two
functions:

- **`getenv`** (21): production `src/cli/keygen/keygen.c:19`,
  `src/secrets/keyfile.c:17,31`, `luavm.c:24,27,55`, `workflow.c:1869`
  (`QWE_TEST_GRACE_MS`); test-only `oom_shim.c:28,84`; tests `TEST_TMPDIR` in nine
  test files, and `QWE_LUA_COVERAGE`/`QWE_OOM_PROBE_TIMEOUT` in three.
- **`readdir`** (4): `luafs.c:33` (production), `oom_test.c:96`, `proc_test.c:169`,
  `workflow_test.c:54`.

Not in the 25, because it is only compiled under `bazel coverage` (`-DQWE_GCOV`)
and the gate never lints that configuration (ticket 05): `src/kernel/gcov.h`'s
`qwe_gcov_dump` calls `getenv("QWE_LUA_COVERAGE")`. It is in scope here too.

## Fix

**`getenv`.** Read the environment in one place, once, and pass values down,
instead of every function reaching into the process environment.

- `src/kernel/env.{c,h}` (`//src/kernel:env`): `struct qwe_env` and
  `qwe_env_load(struct qwe_env *)`, which reads the product's names (`HOME`,
  `QWE_LUA_COVERAGE`, `COVERAGE_DIR`, `QWE_TEST_GRACE_MS`, and whatever
  `keygen.c`/`keyfile.c` read). Called once from `main`, before any other work,
  and the struct is passed down to `luavm.c`, `workflow.c`, `keyfile.c`, `keygen.c`.
- Test code uses the same file: a lookup-by-name form
  (`qwe_env_get(const char *name)` or a names-list variant) that the
  `src/testing/` helper for `TEST_TMPDIR` and `oom_shim.c`'s two reads call. The
  values are pointers into the environment, not copies, so `oom_shim` can call it
  from inside an allocator wrapper without allocating.
- `gcov.h`: `qwe_gcov_dump` takes the flag from the loaded struct (a
  `qwe_gcov_init(const struct qwe_env *)` at startup, or a parameter), not from
  `getenv`.

`env.c` is then the **one** `getenv` call in `src/` and `tools/`, and it stays a
`getenv`, with a `NOLINTNEXTLINE(concurrency-mt-unsafe)` naming this ticket and the
reason: called before any other work, and `//src/cli:no_threads_test` fails if
the binary can create a thread. Ticket 28 lists it in `tools/ci/exceptions.txt`.

Do **not** read `environ` (or `main`'s `envp`) instead to make the check go
quiet. It has the same thread-safety property as `getenv`; the check only goes
silent because it does not model a variable. That is an exception that no
longer looks like one, which an assessor rates worse than a visible, single,
justified `NOLINT`.

**`readdir`.** `readdir` shares one `struct dirent` per `DIR *` (the concern is
two threads on one `DIR *`, which no code here does). The safe form is
`scandir(3)` (allocates a list, no shared state; not in the check's set, checked
with clang-tidy-20) or `readdir_r` (deprecated, avoid). `luafs.c:33` builds a Lua
table of names; `scandir` with no sort, or `qwe_strv` (ticket 10) built from it,
is the same code. Note ticket 02's rule: the `scandir` list is C heap, so it must
not be live across a Lua call that can raise (copy the names into Lua after, or
build the table and free the list with no raise in between).

The `FunctionSet` narrowing then goes from `.clang-tidy`.

## Acceptance criteria

- [ ] `.clang-tidy` has no `concurrency-mt-unsafe.FunctionSet`. The gate exits 0.
      `manual: run.sh`
- [ ] `getenv` is called in exactly one place, `src/kernel/env.c`, headers
      included. `manual: grep -rnw getenv src tools --include=*.c --include=*.h`
- [ ] `environ` and `envp` are not read anywhere in `src/` or `tools/`.
      `manual: grep -rnw "environ\|envp" src tools`
- [ ] `readdir` is not called. `manual: grep -rnw readdir src tools`
- [ ] Behaviour is unchanged: the e2e cases that depend on `QWE_TEST_GRACE_MS`,
      `HOME`, coverage variables still pass, and `bazel coverage` still records
      forked children's lines (the `gcov.h` change). Plus a unit test for
      `qwe_env_load` (set, unset, empty, very long value), without `setenv`: one
      `cc_test` per case with Bazel's `env = {...}` attribute (ticket 18).
      `unit: src/kernel/env_test.c`
- [ ] The doc row is gone. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
