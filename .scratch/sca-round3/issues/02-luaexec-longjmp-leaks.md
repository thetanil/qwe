# 02: `luaexec.c` leaks on a Lua error

Status: resolved
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

- [x] A test that feeds both functions a wrong-typed argument (`{ A = {} }`,
      `{ "true", {} }`, `{ "true", 5 }` where a number is allowed, a table with
      a non-string key) and asserts the error message. It is a Lua test run
      through `luarun`, so `--config=asan` (`asan.yml`, on every push to main)
      runs it under LeakSanitizer. `unit: src/kernel/luaexec_test.c` or a new
      `luaexec_boundary_test.lua`
- [x] Red first: the test fails under `bazel test --config=asan` before the fix
      (LeakSanitizer names `exec_run` and `exec_preamble`), passes after.
      `manual: the ticket's Comments, with the LeakSanitizer output`
- [x] `valgrind --leak-check=full` on the two calls above reports no definite
      leak. `manual: bazel test --config=valgrind` or the command in Comments
- [x] No `malloc`/`calloc`/`realloc`/`strdup` result, open descriptor, `FILE *`
      or unreaped child is live across a Lua API call that can raise, in
      `luaexec.c`. `manual: read each function; the Comments list them and say
      which of the two patterns each uses`
- [x] A raise after the fork in `exec_run` (force one, e.g. a Lua allocation
      failure through the OOM harness, or a test-only hook) leaves no descriptor
      open and no zombie. `unit: count /proc/self/fd and waitpid(-1, WNOHANG)
      before and after, as proc_test does for fds`
- [x] `bazel test //...` and the coverage check are green.

## Comments

Fixed by pattern 1 (validate first, allocate after) everywhere it was
possible, and pattern 2 (own the buffer in a Lua userdata with a `__gc`, new
`src/kernel/luaown.{h,c}`, `qwe_lua_own`/`qwe_lua_own_finish`/
`qwe_lua_own_pushlstring`) at the two spots a C buffer has to be handed to a
Lua API call that can itself raise while copying it.

Per function:

- `exec_run`, argv (line ~87 pre-fix): was `calloc` then, per element,
  `strdup(luaL_checkstring(...))` -- a non-string element raised with earlier
  elements and the array already allocated. Now: a first loop over all `n`
  elements calls `luaL_checkstring` only, leaving each validated value on the
  Lua stack (not popped); nothing on the C heap yet, so a bad element raises
  with nothing to leak. A second loop then `calloc`s and `strdup`s, reading
  each already-a-string value back with `lua_tolstring` (via `lua_tostring`),
  which cannot raise. Pattern 1.
- `exec_run`, the post-fork pushes (pre-fix: `lua_pushinteger` then two
  `lua_pushlstring`s with `out.data`/`err.data` still `malloc`'d): by this
  point argv is freed, and the descriptors and the child are already closed
  and reaped unconditionally (this was already true before the fix -- the
  close/wait code does not run only on success). So the one remaining risk
  was the two heap buffers. Pattern 2: `qwe_lua_own` transfers each buffer to
  a Lua userdata immediately before the push that copies it
  (`qwe_lua_own_finish`), so a raise during that copy frees the buffer via
  the userdata's `__gc` instead of losing it. Also moved `free(argv[...])`
  to right after the fork (nothing after it needs argv), so it is off this
  list entirely rather than merely freed before the risk window.
- `exec_preamble`, names/values (pre-fix: doubling `realloc`, with
  `luaL_checkstring(L, -2)` on the *key* -- itself a bug independent of this
  ticket: coercing a key in place is documented (Lua manual, `lua_tolstring`)
  to confuse `lua_next`): replaced with a single traversal that type-checks
  with `lua_type` (never coercing) and copies each validated key into a
  fresh Lua array (`karr`); nothing is on the C heap during this traversal.
  A second, ordinary bounded `for (i = 0; i < n; i++)` loop (not a second
  `lua_next` traversal -- see below) then `malloc`s the arrays sized exactly
  `n` and fills them via `lua_rawgeti`/`lua_rawget` on `karr`/the table,
  neither of which can raise on an already-known-good key. Pattern 1.
- `exec_preamble`, the final push (pre-fix: `lua_pushlstring(L, out, len)`
  then `free(out)`, `out` from `qwe_preamble_build`, live during the push):
  same as `exec_run`'s pushes. Pattern 2, `qwe_lua_own_pushlstring`.

One implementation detour worth recording: the first cut of the
`exec_preamble` fix used two independent `lua_next` traversals (count, then
fill), matching `exec_run`'s two-pass shape. `tools/clang-tidy/run.sh`
(clang-analyzer-core.NullDereference, then -core.UninitializedArgValue) flagged
it: the analyzer cannot see that traversing the same, unmodified table twice
with `lua_next` (an opaque call to it) yields the same count both times, so it
explores "first traversal saw 0 entries" together with "second traversal saw
some" as though the two were independent. That is not a false-positive worth
suppressing -- the fix above (copy keys into a Lua array on the one real
traversal, then revisit by index) is a genuine improvement: pass 2 is now a
plain bounded C loop the analyzer (and a reader) can just count, not a second
opaque iteration relying on an invariant nothing states.

Red-first evidence (`tools/valgrind`'s `luarun` harness, the ticket's own
repro, run before the fix -- see git history of this ticket's branch for the
one-line revert used to capture this):

```
$ valgrind --leak-check=full ./bazel-bin/src/kernel/luarun /tmp/repro.lua
false   bad argument #4 to '?' (string expected, got table)
false   bad argument #3 to '?' (string expected, got table)
==HEAP SUMMARY==
==   in use at exit: 157 bytes in 4 blocks
==29 (24 direct, 5 indirect) bytes in 1 blocks are definitely lost==
==   at calloc / exec_run
==64 bytes in 1 blocks are definitely lost==
==   at realloc / exec_preamble  (x2, names and values)
==LEAK SUMMARY: definitely lost: 152 bytes in 3 blocks==
```

Same repro after the fix: `All heap blocks were freed -- no leaks are
possible`, `ERROR SUMMARY: 0 errors`. `bazel test --config=asan
//src/kernel:luaexec_test` also passes clean (LeakSanitizer silent).

`qwe_exec_run_test_force_raise` (`luaexec_testhook.h`) is a test-only hook,
the ticket's explicit fallback: LuaJIT's default build allocates its Lua heap
through its own `lj_alloc.c` mmap arena, not libc `malloc`
(`third_party/luajit/BUILD`'s `LUAJIT_USE_SYSMALLOC` is on only for fuzz
builds), so `src/kernel/oom_shim.c`'s `--wrap=malloc` cannot reach a genuine
LuaJIT allocation failure inside `lua_pushlstring`. The hook raises at the
exact point such a failure would, with the buffer already wrapped by
`qwe_lua_own` (proving pattern 2 actually holds, not just that fds/pid were
already safe by construction); the test that exercises it
(`forced_raise_after_fork_leaks_nothing`) checks fd count (`dup(0)`, close,
compare) and `waitpid(-1, WNOHANG)` for a zombie, both unchanged.

`bazel test //...`: 262 pass, 3 skipped (pre-existing, unrelated).
`bazel run //tools/coverage:check`: 0 files below 85%. `tools/clang-tidy/run.sh`:
exit 0. `src/kernel/alloc_audit.txt`'s count for `luaexec.c` (5) is unchanged --
the two-pass rewrites replaced calls one-for-one (a doubling `realloc` became
two `malloc`s, still one bare-call site each after `grep -c`).
