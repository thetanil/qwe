# Implemented with Claude using Matt Pocock Skills

https://github.com/mattpocock/skills


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
