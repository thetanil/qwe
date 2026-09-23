# 18: Lua load/runtime errors must fail gracefully, never as an unprotected panic

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

Diagnosed while chasing an unrelated CI crash under `--config=release`: a LuaJIT VM
correctness bug at `-O2` (fixed for now by pinning the `luajit` `cc_library` to `-O0` in
`third_party/luajit/BUILD`, this feature's 04/05 follow-up work) caused
`pcall(require, 'rex_pcre2')` inside `third_party/lua-schema` to fail to catch its own
error. It escaped all the way to LuaJIT's default `lua_atpanic` handler, which printed
`PANIC: unprotected error in call to Lua API (...)` to stderr and called `exit()`.

The compiler bug is fixed, but the incident exposed a real, independent architectural
weakness: qwe registers **no custom panic handler**, and several call sites in
`src/kernel/workflow.c` invoke Lua via the unprotected `lua_call()` rather than
`lua_pcall()` -- as of this writing, lines 467, 470, 875, 878, 1424, 1427, 1444, 1447, 1522
and 1525 (`grep -n 'lua_call(' src/kernel/workflow.c` to refresh; line numbers will drift,
and it is the only file with any at the moment). Any error in Lua reachable from one of
those -- a bug in our own bootstrap Lua, a bad project plugin, a future embedded-module
change -- has the same failure mode as this incident: raw panic text and an abrupt exit,
not a clean qwe error message.

Two independent hardenings:

- **A safety net.** Register a `lua_atpanic` handler in `qwe_lua_new()`
  (`src/kernel/luavm.c`) that formats whatever is on the stack as a normal qwe error
  (`qwe: internal error: <message>`) on stderr and exits `QWE_EXIT_FAILED`, instead of
  relying on LuaJIT's default handler's raw `PANIC:` text. This alone turns any future
  unprotected-panic incident from crash-shaped, hard-to-search output into a readable,
  scriptable failure -- the direct answer to "there should not be a segfault, there should
  be a graceful failure before the crash."
- **Fewer things for the net to catch.** Audit every `lua_call()` site above against what
  it invokes. Fully-vendored, always-correct bootstrap Lua we already test to death can
  stay `lua_call`. Anything that reaches project-supplied Lua (`.qwe/plugins/`), a
  workflow's own strings (templates, `with:` values interpolated into Lua), or third-party
  code (lua-schema) should become `lua_pcall`, with its failure turned into whatever clean
  error path callers already handle there (a plugin error, a validate error, etc.), not a
  new one.

Out of scope: file-not-found and similar I/O errors are already handled well elsewhere
(`load_workflow`'s own file-read error path, ticket 06's `file.ensure`/etc. work). This
ticket is specifically about the Lua-execution-time class of failure the panic incident
exposed.

## Acceptance criteria

- [x] A registered `lua_atpanic` handler formats an unprotected Lua error as
      `qwe: internal error: <message>` on stderr and exits `QWE_EXIT_FAILED`, never
      LuaJIT's raw `PANIC:` text: `unit: src/kernel/luavm_test.c` (deliberately trigger
      one, e.g. via a test-only embedded module that raises outside any pcall)
- [x] Every remaining `lua_call()` in `src/kernel/workflow.c` is either justified in a
      comment (fully-vendored, always-correct Lua) or converted to `lua_pcall` with a
      clean failure path: `manual: read the diff, confirm each remaining lua_call site's
      comment`
- [x] A project plugin (`.qwe/plugins/`) with a Lua syntax error, or one requiring a
      genuinely missing module, fails the step/job cleanly (a normal plugin-error outcome
      and message), not a crash: `e2e:` extend `project_plugin_loads`'s negative-case
      coverage, or a new `project_plugin_load_error` case
- [x] `bazel test //...` green; the coverage floor holds

## Comments

- Root-caused during this feature's 04/05 follow-up work (CI firefighting, not one of the
  original 16 tickets): the actual bug was LuaJIT miscompiled at `-O2`. This ticket is the
  "make sure a bug like it is never this loud again" follow-up, not a re-fix of that
  compiler issue.
- See ticket 17 for the specific missing module (`rex_pcre2`) that triggered this
  incident -- a real, separately tracked gap, not itself the bug this ticket is about.
- Registered `lua_panic()` in `qwe_lua_new()` (`src/kernel/luavm.c`): formats
  `qwe: internal error: <message>` to stderr and `exit(1)`. `luavm` sits below
  `:kernel` in the bazel graph (`:kernel` depends on `:luavm`), so it cannot include
  `src/kernel/qwe.h` for the `QWE_EXIT_FAILED` symbol without a cycle; the literal `1`
  carries a comment pointing at the real constant, and `src/kernel/luavm_test.c`
  cross-checks the child's exit code against it.
- Audited all ten `lua_call()` sites in `src/kernel/workflow.c`: every one is
  `require("qwe.inventory")` or a call into it (`.use`, `.host`, `.max_sessions`,
  `.disabled`) -- fully-vendored bootstrap Lua (`src/kernel/lua/inventory.lua`), never
  project-supplied. Each site now has a one-line comment saying so; none were converted.
  No other file in the kernel has an unprotected `lua_call()`.
- The third criterion turned out to already be handled at the Lua level, independent of
  the C-API hardening above: `qwe.plugins.run_step` (`src/kernel/lua/plugins.lua`)
  already wraps a plugin's `open()`/`check`/`apply` in a Lua-level `pcall`, so a
  `require()` of a genuinely missing module inside `apply` was already a clean
  `plugin-error`, not a crash -- confirmed by the new
  `tests/e2e/project_plugin_load_error` case. A plugin's Lua *syntax* error is caught
  even earlier, statically, by `qwe.plugincheck` during `load_workflow` (shared by `qwe
  run` and `qwe validate`), already covered for `qwe validate` by the existing
  `plugin_files_malformed` case. Neither path touches the ten `lua_call()` sites above.
- New e2e case pins job scheduling with `max-parallel: 1` so the failing job's stdout
  interleaves deterministically with the job that keeps running.
