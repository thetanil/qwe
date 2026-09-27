# 03: Every C function Lua can call, with wrong arguments

Status: ready-for-agent
Category: bug
Type: task
Blocked by: 02

## What

Ticket 02 is one instance of a class: **C memory or a C resource held across
a call that longjmps.** There are 28 functions with a `lua_State *` parameter
outside tests: `luacbor.c` 10, `luaexec.c` 6, `luasecrets.c` 5, `luafs.c` 4,
`luavm.c` 3. `luaL_check*`, `luaL_error` and `lua_error` appear in `luaexec.c` 14
times, `luasecrets.c` 6, `luacbor.c` 4 and `luafs.c` 3. `jobs.c` and
`workflow.c` call Lua too (`qwe_jobs_load`'s `strdup(lua_tostring(L, -1))`
takes NULL on a non-string `needs` entry: that is a `strdup(NULL)`, whatever
validates it earlier).

Two things to build:

1. **An audit**, written into this ticket's Comments as a table: every function
   that takes a `lua_State *`, in `src/` (not just the files above: `jobs.c`,
   `workflow.c`, `validate.c`), with (a) the Lua API calls that can raise, (b) the
   C allocations, `FILE *`s and file descriptors live at each, (c) fixed or
   already safe, and how.
2. **A test matrix** that calls each function reachable from Lua with every
   wrong type for every argument (`nil`, boolean, number, string, table,
   function, userdata; too few and too many arguments) under the sanitizers,
   asserting a Lua error (or a documented result) and no leak. Generate the
   cases in Lua from a table of `{module, function, argument shapes}` rather than
   writing them by hand, so a new function without a row is visible.

`lua_pushlstring`, `lua_createtable` and friends raise on a Lua allocation
failure. The existing OOM harness (`oom_shim`) can make LuaJIT's allocator fail
too, so add the Lua-side failure to the matrix where it is cheap. If it is not,
say which functions were not covered and why in the Comments.

## Acceptance criteria

- [ ] The audit table is in the Comments and covers every function that takes
      a `lua_State *` outside tests, in any parameter position (`send_result(L,
      idx, fd)` included). `manual: grep -rnE "lua_State \*[A-Za-z_]+[,)]" src
      --include=*.c --include=*.h | grep -v _test`, prototypes and definitions
      deduplicated, matches the table's rows. (A pattern ending in `L)` only
      finds functions whose sole parameter is `L`: 32 lines against 57.)
- [ ] The wrong-argument matrix exists and runs under `bazel test --config=asan`
      (LeakSanitizer) on every push. `unit: src/kernel/lua_boundary_test.lua` (or
      the name chosen)
- [ ] A function added later without a row fails a test. `unit: the matrix test
      compares its table to the functions each luaopen_* registers`
- [ ] Every function the audit marks unsafe is fixed by ticket 02's rule, each
      with a red-then-green case in the matrix. `manual: Comments`
- [ ] `qwe_jobs_load` does not pass NULL to `strdup`. `unit: a job whose
      needs contains a boolean`
- [ ] `bazel test //...`, `--config=asan`, `--config=ubsan` and the coverage
      check are green.

## Comments
