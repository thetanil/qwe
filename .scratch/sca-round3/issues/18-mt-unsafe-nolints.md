# 18: The no-threads `NOLINT`s

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 17

## What

Three `NOLINTNEXTLINE(concurrency-mt-unsafe)` exist. Each rests on "qwe has no
threads", which is tested (`//src/cli:no_threads_test`, a symbol check on
`qwe-debug`) but is still a premise in a justification an assessor must accept.
Remove the premise's job:

1. `src/kernel/luavm.c:85`: `exit(1)` in the Lua panic handler.
2. `src/testing/env.h:17,23`: `setenv`/`unsetenv` in the test helpers.

(Ticket 17 leaves one justified `NOLINT` of the same family, on the single
`getenv` in `env.c`. This ticket does not remove that one.)

## Fix

1. **`lua_panic`.** `exit` runs `atexit` handlers and flushes stdio; the concern
   is racing with another thread that is doing the same. `_exit(1)` after an
   explicit `fflush(NULL)` does what `exit` did without running handlers
   (there are none of ours; check `grep -rn atexit src`) and without the flagged
   call (`exit` is in the check's set; `_exit` and `fflush` are not). `_exit`
   skips the gcov write a normal exit performs in the coverage build, so call
   `qwe_gcov_dump()` (`src/kernel/gcov.h`, already used before `_exit` in
   `proc.c`) first; the coverage check must not lose the panic tests' lines.
2. **Test environment helpers.** The tests set `QWE_LUA_COVERAGE`, `HOME`, and the
   OOM shim's variables *for the code under test to read with `getenv`*. After
   ticket 17 the code under test takes a `struct qwe_env`, so a test builds the
   struct it wants and never touches the process environment. The five
   `setenv`/`unsetenv` users (`gcov_test`, `luavm_test`, `oom_test`,
   `oom_shim_test`, `encrypt/oom_test`) then have no reason to call either:
   - code under test in the same process: pass the struct;
   - the OOM shim's settings in forked children: the parent sets the shim's
     configuration (a struct in `oom_shim`) before `fork`, and the child
     inherits it with the rest of its memory;
   - a test that `exec`s a binary that must see a variable: the test calls
     `execve` with an explicit `envp` it built. This is test code only; the
     product's spawn (`execvp` in `proc.c`, `luaexec.c`) keeps inheriting its
     environment, which is product behaviour and not this ticket's to change;
   - a test of `qwe_env_load` itself: one `cc_test` per case, each with Bazel's
     `env = {...}` attribute, so the variable is set before the process starts.

   Then delete `src/testing/env.h`.

## Acceptance criteria

- [ ] `grep -rn NOLINT src tools` finds, for `concurrency-mt-unsafe`, only the
      one in `src/kernel/env.c` (ticket 17). `manual: grep`
- [ ] `setenv`/`unsetenv`/`putenv` are not called, and `environ` is not assigned,
      in `src/` or `tools/`. `manual: grep -rnwE "setenv|unsetenv|putenv|environ" src tools`
- [ ] `src/testing/env.h` is gone. `manual: ls`
- [ ] The Lua panic tests (`luavm_test`'s panic case and any e2e that hits it)
      still pass and still show as covered in the coverage report.
      `unit: src/kernel/luavm_test.c` `manual: coverage check`
- [ ] The OOM tests inject the same failures, same count and outcome.
      `unit: the existing oom tests`
- [ ] `//src/cli:no_threads_test` stays: it now guards a design rule and the one
      `env.c` exception. Its header comment is reworded. `manual: read it`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
