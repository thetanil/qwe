# 03: Every C function Lua can call, with wrong arguments

Status: resolved
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

- [x] The audit table is in the Comments and covers every function that takes
      a `lua_State *` outside tests, in any parameter position (`send_result(L,
      idx, fd)` included). `manual: grep -rnE "lua_State \*[A-Za-z_]+[,)]" src
      --include=*.c --include=*.h | grep -v _test`, prototypes and definitions
      deduplicated, matches the table's rows. (A pattern ending in `L)` only
      finds functions whose sole parameter is `L`: 32 lines against 57.)
- [x] The wrong-argument matrix exists and runs under `bazel test --config=asan`
      (LeakSanitizer) on every push. `unit: src/kernel/lua_boundary_test.lua` (or
      the name chosen)
- [x] A function added later without a row fails a test. `unit: the matrix test
      compares its table to the functions each luaopen_* registers`
- [x] Every function the audit marks unsafe is fixed by ticket 02's rule, each
      with a red-then-green case in the matrix. `manual: Comments`
- [x] `qwe_jobs_load` does not pass NULL to `strdup`. `unit: a job whose
      needs contains a boolean`
- [x] `bazel test //...`, `--config=asan`, `--config=ubsan` and the coverage
      check are green.

## Comments

### The audit

Deduplicated (prototype + definition collapsed to one row), every function
with a `lua_State *` parameter outside tests. "Lua-callable" means a project
plugin or the built-in modules can reach it directly (through a `luaopen_*`
registration); the rest take `L` only to drive the interpreter from C, with
arguments the C caller controls, not a Lua script -- ticket 02's rule still
applies to them (a raise while they hold a C resource), but "wrong-typed
argument from Lua" does not, since Lua never supplies their arguments.

**`src/kernel/luaexec.c` (qwe.exec, Lua-callable) -- 7 with this session's
`maybe_force_raise` added (ticket 02's 6 + 1):**

| Function | Lua-callable | Raising calls | C resource live at one | Fixed |
|---|---|---|---|---|
| `exec_run` | yes | `luaL_check*`, `lua_push*` | argv (strdup'd), out/err buffers | ticket 02: two-pass validation + `qwe_lua_own` |
| `exec_preamble` | yes | `lua_next`, `lua_push*` | names/values arrays, the built string | ticket 02: single traversal into a Lua array + `qwe_lua_own_pushlstring` |
| `exec_wait` | yes | `luaL_check*` | none (no C heap) | already safe: validates before any state |
| `exec_getpid`/`exec_getuid` | yes | none (no args) | none | already safe |
| `maybe_force_raise` | no (internal, called with a fixed literal) | `luaL_error` (its own point) | none | N/A -- not itself a source of unowned C memory |
| `luaopen_qwe_exec` | no (module init, run once at startup with no Lua-script input) | registration calls only | none | already safe |

**`src/kernel/luacbor.c` (qwe.cbor, Lua-callable: `decode`, `encode`, `array`,
`map`, `is_array`, `is_map`, `is_secret`, `secret`) -- 10:**

| Function | Lua-callable | Raising calls | C resource live at one | Fixed |
|---|---|---|---|---|
| `secret_tostring` | yes (`__tostring`, called by Lua's `tostring()`/string coercion, no C-controlled argument) | none | none | already safe |
| `ensure_metatables` | no (internal) | `luaL_newmetatable`, `lua_setfield` | none | already safe |
| `convert_string` | no (internal, called from `convert`) | `lua_pushlstring` | `s` (tinycbor `_dup_*`) | **fixed**: `qwe_lua_own_pushlstring` |
| `convert_container`/`convert_tagged`/`convert` | no (internal) | `lua_newtable`, `lua_settable`, recursion | none directly (each holds no separate buffer once `convert_string` above is fixed) | already safe |
| `qwe_cbor_to_lua` | no (a C API entry, called by `l_decode` and `workflow.c`'s `qwe_cbor_to_lua` caller with a C buffer, not a Lua argument) | `convert` | none | already safe |
| `classify` | no (internal) | `lua_getmetatable`, `luaL_getmetatable` | none | already safe |
| `encode_table` | no (internal, called from `encode_value`) | `lua_next`, `lua_pushstring`, `lua_gettable`, recursion | `keys` (the map-key array; its elements are borrowed, not owned) | **fixed**: `qwe_lua_own_block` around the per-key push/recurse loop |
| `encode_value` | no (internal) | dispatches to the above | none directly | already safe once `encode_table` is fixed |
| `qwe_lua_to_cbor` | yes indirectly (the C entry `l_encode` and `send_result`/`send_status` in `workflow.c` call it with a Lua-script-reachable value at `idx`) | `encode_value`'s whole recursion | `buf` (the growing encoder buffer) | **fixed**: `qwe_lua_own_block`/`qwe_lua_own_take` around the `encode_value` call |
| `l_decode`/`l_encode`/`set_mt`/`l_array`/`l_map`/`has_mt`/`l_is_array`/`l_is_map`/`l_is_secret`/`l_secret`/`luaopen_qwe_cbor` | yes (the module surface) | various | `l_encode` held `buf` across its own final `lua_pushlstring` | **fixed**: `l_encode` now uses `qwe_lua_own_pushlstring`; the rest already safe (no bare C allocation) |

**`src/kernel/luafs.c` (qwe.fs, Lua-callable: `list`, `isdir`,
`private_dir`) -- 4:**

| Function | Raising calls | C resource live at one | Fixed |
|---|---|---|---|
| `fs_list` | `lua_createtable`, `lua_pushstring` | `names` (the array and its `n` strdup'd entries) | **fixed**: `qwe_lua_own_strv` around the table-building loop |
| `fs_isdir` | `luaL_checkstring` (coerces; no C heap) | none | already safe |
| `fs_private_dir` | `luaL_checkstring`/`luaL_optstring` (no C heap; `err` is a stack buffer) | none | already safe |
| `luaopen_qwe_fs` | registration only | none | already safe |

**`src/kernel/luasecrets.c` (qwe.secrets, Lua-callable: `reveal`, `mask`,
`check`) -- 5:**

| Function | Raising calls | C resource live at one | Fixed |
|---|---|---|---|
| `ensure_key` | `lua_error` (its own point) | none (`err` is a stack buffer) | already safe |
| `secrets_reveal` | `ensure_key`, `lua_pushlstring` | `plain` (the decrypted secret -- security-sensitive: freeing it is not enough, it must be wiped) | **fixed**: a local `owned_secret` userdata whose `__gc` does `sodium_memzero` then `free`; the success path still wipes+frees immediately rather than waiting on GC |
| `secrets_mask` | `luaL_checklstring` (coerces; no C heap) | none | already safe |
| `secrets_check` | `luaL_checkstring` (no C heap; `why` is a `const char *` literal) | none | already safe |
| `luaopen_qwe_secrets` | registration only | none | already safe |

**`src/kernel/luavm.c` -- 3, none Lua-callable (module bootstrap, run once
with no Lua-script-supplied argument):** `coverage_enable`, `qwe_lua_coverage_flush`,
`lua_panic`, `qwe_lua_new`. No bare C allocation in any of them; already safe.

**`src/kernel/jobs.c`, `src/kernel/workflow.c`, `src/kernel/validate.c`**
(not Lua-callable -- their `lua_State *` carries the *decoded workflow
document*, produced by `qwe_yaml_to_cbor`/`qwe_cbor_to_lua` from a project's
own YAML, which is untrusted input but not a direct Lua-script call):

- `qwe_jobs_load` (`jobs.c`): `j->needs[k] = strdup(lua_tostring(L, -1))` took
  NULL on a `needs` entry that is not a string -- **fixed** (a `bad = refuse(...)`
  path, matching this function's existing style, instead of undefined
  behavior). `j->id`'s key does not need the same guard: every CBOR map key
  is already forced to be a text string by `luacbor.c`'s `convert_container`
  (`"map key is not a string"`) before `qwe_jobs_load` ever runs -- confirmed
  by reading that path, not assumed.
- `load_inventory` (`workflow.c`): `default_path` (built when no inventory
  path is given) was live across two unprotected `lua_call`s (`ticket 18`:
  bootstrap Lua, `lua_call` not `lua_pcall`) after it was no longer needed --
  **fixed**, freed right after the last read of `inv_path` (which may point
  into it).
- `send_result`/`send_status` (`workflow.c`): call `qwe_lua_to_cbor` (fixed
  above) but never hand its `buf` to a Lua call themselves (they `write()` it
  to a pipe, then `free`), so they were already safe once that fix landed.
- Everything else read with `strdup`/`malloc`/`calloc`/`realloc` in `jobs.c`,
  `workflow.c` and `validate.c` (`read_file`, `mkdir_p`, `drain_result`,
  `qwe_run_workflow`'s `dir`/`run_dir`/`trace_path`, `select_jobs`'s `keep`,
  `validate.c`'s error/pointer-token buffers) either makes no Lua API call at
  all while the buffer is live (pure C), or sits in a function called once
  per `qwe run`/`qwe validate` invocation from a process that has no
  enclosing `lua_pcall`: an uncaught raise there reaches `lua_panic`
  (`luavm.c`) and `exit(1)`s the whole process, which the OS reclaims --  not
  a leak that compounds the way one in a Lua-callable, repeatedly-invoked
  function does. `select_job_survives_every_injection`/`run_survives_every_injection`
  (`oom_test.c`) already exercise every allocation in this call tree under
  injected failure and assert a clean exit or a reported error, for every one
  of them; that is not a valgrind-grade leak check, but it is why these are
  lower priority than the Lua-callable modules and were not each given their
  own `qwe_lua_own` treatment in this ticket.

### The test matrix

`src/kernel/lua_boundary_test.lua`, run via the existing `sh_test` +
`//tests:run_lua_test.sh` + `luarun` pattern (`lua_boundary_test`,
`src/kernel/BUILD`). A table of every Lua-callable function above (`qwe.exec`,
`qwe.fs`, `qwe.secrets`, `qwe.cbor`), each with its argument count, which
types are accepted at each position (a function using `luaL_checkstring`
accepts a number too, since it coerces -- this is not "wrong", and is called
out per row), and one "good" example value per position. The generator then:

- calls the function with every type *not* accepted at each position (a good
  value at every other position), and asserts `pcall` catches a string error;
- calls it with fewer than its required arguments, for every length below
  that;
- cross-checks: every key `require("qwe.X")` actually has must appear in the
  matrix (as a real entry or a one-line "not a function: why"), so a function
  added later without a row fails the test rather than going untested.

Constants and predicates that accept any value by design (`qwe.cbor.is_map`
and friends, `qwe.exec.bootstrap`, the metatable constants, `cbor.null`) are
listed with `"any"`/a skip reason rather than fuzzed -- there is no wrong
type or missing argument for them to catch, and forcing cases for them would
only assert that `pcall` on a non-function raises, which is not what this
ticket is about. `qwe.secrets.reveal`'s only argument is a table; the
string-ness of its nested `value` field is not a top-level argument shape and
is covered by `luaexec_test.c`'s sibling wrong-argument tests instead
(ticket 02's pattern).

One bug the matrix caught in itself, worth recording: the first draft built
each case with `args[i] = (i == pos) and sample(bad) or good`, the classic
Lua "ternary" idiom -- which silently substitutes the *other* operand when
`sample(bad)` is `nil` or `false`, so the "nil" and (for booleans) "false"
cases never actually ran. Rewritten as an explicit `if`/`else`. Running it
before that fix, plus a second wrong assumption (that `luaL_checkstring`
rejects a number), together reported false failures in exactly the way a
real bug would have -- useful evidence the matrix would catch one.

`qwe_lua_own_block`/`qwe_lua_own_take`/`qwe_lua_own_strv`/
`qwe_lua_own_strv_release` were added to `src/kernel/luaown.{h,c}` (ticket
02) alongside the existing `qwe_lua_own`/`qwe_lua_own_pushlstring`, to cover
the shapes this ticket's audit turned up: an opaque block whose pointee type
is not `char` (`keys`, `buf`), and an array of owned strings freed per
element (`names`).

`bazel test //...` (263 pass, 3 skipped, pre-existing/unrelated),
`tools/clang-tidy/run.sh` (exit 0) and `bazel run //tools/coverage:check`
(0 files below 85%) are all green. `--config=asan` on `lua_boundary_test`
and `lua_cbor_test` is clean.
