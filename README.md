# Implemented with Claude using Matt Pocock Skills

## Status

[![tests](https://github.com/thetanil/qwe/actions/workflows/tests.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/tests.yml)
[![asan](https://github.com/thetanil/qwe/actions/workflows/asan.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/asan.yml)
[![ubsan](https://github.com/thetanil/qwe/actions/workflows/ubsan.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/ubsan.yml)
[![valgrind](https://github.com/thetanil/qwe/actions/workflows/valgrind.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/valgrind.yml)
[![coverage](https://github.com/thetanil/qwe/actions/workflows/coverage.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/coverage.yml)
[![nightly](https://github.com/thetanil/qwe/actions/workflows/nightly.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/nightly.yml)
[![release](https://github.com/thetanil/qwe/actions/workflows/release.yml/badge.svg)](https://github.com/thetanil/qwe/actions/workflows/release.yml)

https://github.com/mattpocock/skills

# up next

ci to release


# For plugin authors

A project plugin lives next to the workflow, in `.qwe/plugins/<name>/`, and a step
names it with `uses: <name>`:

```
.qwe/plugins/my.plugin/
  schema.json   { "with": <JSON schema of the step's with:>, "outputs": { name: <schema + "secret": bool> } }
  plugin.lua    the code
```

`schema.json` is checked against qwe's strict metaschema. `outputs` is optional, and
every output must say `"secret": true` or `"secret": false`.

## What `plugin.lua` looks like

```lua
local M = {}

local function quote(s) return "'" .. s:gsub("'", "'\\''") .. "'" end

-- Returns true if apply has work to do (and, optionally, a table of state).
function M.check(with, ctx)
  local r = ctx.backend:run("test -f " .. quote(with.path))
  return r.code ~= 0
end

function M.apply(with, ctx)
  ctx.backend:run("touch " .. quote(with.path))
end

return M
```

A run-like plugin exports `M.argv(with, ctx)` instead of `check` and `apply`, and returns
the command to execute (see the built-in `run` plugin). Commands go through
`ctx.backend` (the job's target); a plugin's Lua itself runs on the operator host, in a
forked child, once per step. An error raised in `check` or `apply` fails the step with
reason `plugin-error`.

## What `plugin.lua` may not do

**A plugin file has no top-level code.** Everything outside a function runs when the file
is loaded, so qwe refuses any of it at `qwe validate` and `qwe run`, from the source,
without running the plugin. The top level may contain only:

- `local` declarations of constants, function literals and table constructors (of the same),
  and reads of names, fields and operators on them;
- `M.name = value` assignments (which is what `function M.name() ... end` is);
- `require("...")` of a module qwe itself provides (for example `qwe.*`);
- one final `return` of the module table.

Refused, with `file:line:col`: a call, a method call, a loop, `if`, `do`, reassigning a local,
`require` of any other file. Work, including setup, belongs in `check` or `apply` or in a
function they call.

`check` and `apply` must both be exported as functions (a function literal, or a local
function), or `argv` for a run-like plugin. `qwe validate` guarantees this from the source, so
a validated plugin always has an `apply`.

These rules are deliberately strict, and they are a current limit rather than a
principle. The known consequences:

- `M.apply = pick()` is refused as a call, even if it would return a function. Write
  `function M.apply(...)`.
- A plugin cannot `require` a shared file from the project; it is one file, or it uses qwe's modules.
- There is no load-time hook (an `init`); nowhere to put setup except `check` and `apply`.

If one of these blocks you, ask: they can be loosened for a specific need, and the check
is `src/kernel/lua/pluginshape.lua`.

## Trust

Plugins run with your privileges on the operator host. `qwe.strict` (which turns a misspelled
or undeclared global into an error) prevents mistakes; it is **not** a sandbox, and a plugin
still has `io`, `os.execute` and the rest of the interpreter. The workflow directory and the
inventory are trusted input: read a workflow directory you did not write before running it
(`qwe validate` runs none of its plugin code). See "Trust boundary" in `CONTEXT.md`.

## Coverage of plugin code

`bazel coverage` measures the Lua that is compiled into the qwe binary: `src/kernel/lua/` and
the built-in plugins under `plugins/builtin/`. A project plugin in `.qwe/plugins/` is read by
path at run time, not compiled in, so it is **not** measured.

To have a built-in plugin measured, add it to the repo like the others:

1. Put it in `plugins/builtin/<name>/` (`plugin.lua`, `schema.json`). The `:plugin_lua` glob in
   `plugins/builtin/BUILD` picks the `.lua` up, and `:shipped` there declares it to the coverage
   tool, so nothing to add for a file in that layout.
2. Register it in `MODULES` in `src/kernel/lua/BUILD` (`plugin.<name>` and `plugin_schema.<name>`),
   which is what compiles it into the binary.
3. A Lua file in a *new* directory needs its own `lua_instrumented` target (see
   `tools/coverage/defs.bzl`) listed in `data` of `//src/kernel:luavm`. Without it Bazel's lcov
   merger drops the file from the report without a warning.
4. Write the tests that should exercise it (a Lua test through `luarun`, or an e2e case under
   `tests/e2e/`). Only code those tests run is counted as hit.

One command runs the coverage and writes the HTML (needs `genhtml`, from the `lcov` package):

```
bazel run //tools/coverage:html            # writes coverage-html/index.html
bazel run //tools/coverage:html -- my-dir  # or into another directory
```

Before you send a change, check that coverage has not dropped:

```
bazel run //tools/coverage:check
```

(See "Keeping coverage from dropping" below.)

Your plugin's page is under `plugins/builtin/<name>/`, with each line marked hit or missed. The
same report covers the C; `docs/coverage.md` has the per-file numbers and how the Lua hook works.

## The binary

`bazel build //src/cli:qwe //src/cli:qwe-debug` produces two statically linked executables
(`bazel-bin/src/cli/`): `qwe`, stripped, which is what ships and what the e2e cases run, and
`qwe-debug`, the same build with its symbols. The sanitizer (`--config=asan`, `ubsan`) and
valgrind builds link dynamically, because a sanitizer runtime cannot be linked statically and
valgrind cannot intercept `malloc` in a static binary; `qwe` keeps its symbols there.

## Keeping coverage from dropping

```
bazel run //tools/coverage:check              # fails if a file has more uncovered lines than floor.txt allows
bazel run //tools/coverage:check -- --update  # after improving coverage: ratchet floor.txt down
```

`tools/coverage/floor.txt` lists, per file under `src/` and `plugins/` (C and Lua), the most
uncovered lines allowed. Untested new code raises a file's count and fails the check; a new file with misses
must be listed (run `--update` once it is tested). It is a `bazel run`, not a `bazel test`,
because a test cannot itself run `bazel coverage`; run it in CI. Commit `floor.txt` changes on purpose.
