# 09: Write down that a workflow directory is trusted code

Status: ready-for-agent
Category: docs
Type: task
Blocked by: none

## What

`qwe run w.yaml` loads every project plugin under `<workflow dir>/.qwe/plugins/`
and runs its Lua. That Lua runs under `qwe.strict`, and the name invites the
wrong conclusion. `strict.lua` is:

```lua
local base = _G
local env = setmetatable({}, {
  __index = function(_, key)
    local value = rawget(base, key)
    if value == nil then error(...) end
    return value
  end,
  ...
})
```

It proxies the real `_G`. A plugin gets `io`, `os`, `os.execute`, `loadstring`
and everything else the interpreter has. It stops a typo from becoming a silent
`nil` or a leaked global — which is what it is for, and it does it well — but
it is not a sandbox and does not attempt to be one.

So: running `qwe run` on a workflow directory someone else wrote executes their
code as you, before any step runs. That is a perfectly defensible design. It is
the same bargain as `make`, `npm install` or a `.github/` directory, and the
alternative — a real Lua sandbox — is a large piece of work whose value here is
doubtful, since a workflow can already run arbitrary commands through `run:`.

The problem is that this is nowhere stated. It is inferable from `strict.lua`
and from ADR-0002, and a reader who only sees "plugins run under strict
globals" may well conclude the opposite. For a tool heading for safety
certification, the trust boundary has to be written down and owned, not left to
be worked out from a module name.

**What to write.** A short section in `CONTEXT.md`, next to the plugin
definitions, saying plainly:

- The operator host's workflow directory is trusted input. Running a workflow
  from it runs its project plugins' Lua with the operator's privileges, at load
  time, before any step.
- `qwe.strict` catches mistakes, not malice. Name it for what it does.
- `qwe validate` is not a safe way to inspect an untrusted workflow: it loads
  and lints project plugins, which means reading their source and running
  luacheck over it, and it runs `plugincheck` — worth stating exactly how far
  it goes, and whether any of it executes plugin code.
- The inventory is trusted the same way, since it names hosts qwe will connect
  to and holds the secrets a job can read.

The last point needs checking rather than asserting: `plugins.load` reads
plugin source and runs `plugincheck.check` on it during validation, while
`open()` — which actually executes the chunk — is called from `run_step` in the
forked child. Confirm that `qwe validate` never executes plugin code, and then
say so. If it does, that is a finding rather than a documentation task, and this
ticket should be split.

## Acceptance criteria

- [ ] `CONTEXT.md` states the trust boundary: the workflow directory and the inventory are trusted input, and project plugin code runs with the operator's privileges.
- [ ] The `qwe.strict` entry says it prevents mistakes and is not a sandbox, so the name cannot be read as a security claim.
- [ ] It is confirmed by reading `plugins.load`, `plugincheck.check` and `open()` whether `qwe validate` executes any project plugin code, and `CONTEXT.md` records the answer. `manual: trace the call path from qwe_validate_workflow to plugins.load and note where open() is reached`
- [ ] A test pins the answer, so it cannot drift: a project plugin whose top level has a side effect (writing a file) is validated, and the side effect does not happen. `e2e: tests/e2e/validate_does_not_run_plugins/`
- [ ] `docs/workflow-kernel-design.md` §12 links to the boundary rather than restating it.

## Comments
