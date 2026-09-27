# 23: luacheck over all of the repo's Lua

Status: ready-for-agent
Category: enhancement
Type: task

## What

luacheck 1.2.0 is vendored (`third_party/luacheck`) and embedded in the binary.
It runs on a plugin's source at `qwe validate` (`plugincheck.lua`) and, through
`//plugins/builtin:lint_test`, on every built-in plugin's `plugin.lua` and on the
execution-backend Lua. It does **not** run on:

- `src/kernel/lua/*.lua`: `become`, `checkapply`, `compat53`, `inventory`,
  `luacov`, `pluginshape`, `plugincheck`, `plugins`, `strict`, `template`,
  `validate` (about 1,600 lines of the engine's own Lua);
- every `*_test.lua` (`src/kernel/lua`, `src/kernel/schema`, `plugins/builtin`);
- every built-in plugin's `test.lua`;
- `tests/` Lua (the fixtures under `tests/e2e/**/.qwe/` are data: see Fix).

3,411 lines of Lua in all. Measured, luacheck at `--std=luajit` with default
warnings on the uncovered files (2026-09-26): 7 findings.

- `compat53.lua:3,4,6` `utf8` undefined/set/mutated; `:27,28` `math.tointeger`
  undefined/set;
- `luacov.lua:23` unused argument `self`;
- `plugincheck.lua:95` unused argument `name`.

Small numbers, a small ticket, and it is a whole class of code with no analysis.

## Fix

- A `.luacheckrc` at the repo root (`std = "luajit"`, no globals other than what
  the engine provides: `qwe` is a module, not a global) and a Bazel test,
  `//tools/lua:luacheck_test`, that runs the vendored luacheck over **every**
  `.lua` in `src/`, `plugins/`, `tools/` and `tests/`, the same way `lint_test`
  does for the built-in plugins. The test enumerates files by glob so a new
  Lua file is checked without editing the test.
- `compat53.lua`: find out what needs it. If the callers can do without `utf8`
  and `math.tointeger` (LuaJIT has neither; the file exists to give them one), do
  that and delete the file. If they cannot, the polyfill must not write to
  `_G`: return a table (`local compat = require("qwe.compat53")`) and have the
  callers use it, so luacheck's `globals` stays empty. A
  `-- luacheck: globals utf8` comment or a `.luacheckrc` `globals` entry is an
  exception, and is the last resort.
- The two unused arguments: rename to `_` or drop.
- `tests/e2e/**/.qwe/plugins/*` are fixtures, some written to be bad on purpose
  (`plugin_luacheck/untidy`, `strict_globals_*/sloppy`). They are test data, not
  code, and the glob excludes that one path pattern. That is a structural rule
  (one line in the test, one sentence in the doc), not a per-finding exception.
- Plugin `test.lua` and `*_test.lua` run inside `luarun` with test globals: if
  they use a global the test runner defines, prefer requiring it (`local t =
  require("qwe.test")`) to declaring a global to luacheck.

## Acceptance criteria

- [ ] `//tools/lua:luacheck_test` exists and checks every `.lua` file outside
      `third_party/` by glob. `unit: tools/lua/luacheck_test`
- [ ] It reports zero findings, with an empty `globals` and `ignore` in
      `.luacheckrc`. `manual: read .luacheckrc`
- [ ] A throwaway unused local in `src/kernel/lua/template.lua` fails it, and so
      does one in a `*_test.lua` and one in a plugin `test.lua`.
      `manual: add each, see the failure, revert`
- [ ] `compat53.lua` is deleted or no longer writes a global, and the tests that
      used `utf8`/`math.tointeger` still pass. `unit: the existing Lua tests`
- [ ] `bazel test //...` and the coverage check (Lua too) are green.

## Comments
