# 02: `luaexec.c` leaks on a Lua error

Status: ready-for-agent
Category: bug
Type: task

## What

`exec_run` and `exec_preamble` (`src/kernel/luaexec.c`) allocate C memory and then
call Lua API functions that can raise a Lua error, which longjmps out and skips
the `free`s. Reproduced with the built `luarun` under valgrind:

```lua
local exec = require("qwe.exec")
print(pcall(exec.preamble, { A = {} }))      -- 192 + 192 bytes definitely lost per call
print(pcall(exec.run, { "true", {} }))       -- 72 direct + 15 indirect per call
```

Sites, all found by reading after the valgrind run:

- `exec_run`, line 87: `strdup(luaL_checkstring(L, -1))` in a loop, after
  `calloc(n + 1, ...)` and the earlier `strdup`s. A non-string element raises,
  and `argv` and every string in it leak.
- `exec_preamble`, lines 345-346: `luaL_checkstring` on a key and a value after
  `realloc`ed `names` and `values`. Both leak.
- `exec_run`, lines 249-258: `lua_pushinteger`/`lua_pushlstring` run while
  `out.data` and `err.data` are still C memory. They can raise a Lua memory
  error too, and leak both. (Every `lua_push*` that creates a string, and
  `lua_createtable`, `lua_newtable`, `lua_settable`, `lua_next` and `lua_getfield`
  on a table with a metatable, can raise.)
- `exec_preamble`, the `lua_next` loop: `lua_next` raises if the table is
  modified during traversal, again with `names`/`values` live.

clang-tidy and the analyzer do not model longjmp. The sanitizer and valgrind
CI runs do not feed these functions wrong-typed values. So nothing looked.

## Fix

The rule for every C function Lua can call (ticket 03 audits the rest):
**no C resource is live across a call that can raise.** A resource is C-heap
memory, and also a file descriptor, a `FILE *`, a `DIR *`, and a child process
not yet reaped. `exec_run` holds all of these at once: after the fork it has
the pipe ends and the child's pid live, and a raise before `waitpid` leaks the
descriptors and leaves a zombie. Two ways to meet the rule:

1. Validate first, allocate after. Read every argument into the Lua stack with
   `luaL_check*` before the first `malloc`, then do the C work with no Lua API
   call inside it.
2. Where the work needs Lua calls in the middle, keep the buffer in Lua-managed
   memory: `lua_newuserdata` with a `__gc`, or `luaL_Buffer`, so a raise frees it.

Prefer 1. For `exec_run`'s results, build the return values from the C buffers
*after* the last raise point, or hold `out`/`err` in userdata. Descriptors and
the child: close and reap before the first `lua_push*`, or hold them in a
userdata whose `__gc` closes and reaps.

## Acceptance criteria

- [ ] A test that feeds both functions a wrong-typed argument (`{ A = {} }`,
      `{ "true", {} }`, `{ "true", 5 }` where a number is allowed, a table with
      a non-string key) and asserts the error message. It is a Lua test run
      through `luarun`, so `--config=asan` (`asan.yml`, on every push to main)
      runs it under LeakSanitizer. `unit: src/kernel/luaexec_test.c` or a new
      `luaexec_boundary_test.lua`
- [ ] Red first: the test fails under `bazel test --config=asan` before the fix
      (LeakSanitizer names `exec_run` and `exec_preamble`), passes after.
      `manual: the ticket's Comments, with the LeakSanitizer output`
- [ ] `valgrind --leak-check=full` on the two calls above reports no definite
      leak. `manual: bazel test --config=valgrind` or the command in Comments
- [ ] No `malloc`/`calloc`/`realloc`/`strdup` result, open descriptor, `FILE *`
      or unreaped child is live across a Lua API call that can raise, in
      `luaexec.c`. `manual: read each function; the Comments list them and say
      which of the two patterns each uses`
- [ ] A raise after the fork in `exec_run` (force one, e.g. a Lua allocation
      failure through the OOM harness, or a test-only hook) leaves no descriptor
      open and no zombie. `unit: count /proc/self/fd and waitpid(-1, WNOHANG)
      before and after, as proc_test does for fds`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
