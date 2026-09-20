# 11: `qwe validate` executes a project plugin's top level

Status: ready-for-human
Category: bug
Type: task
Blocked by: none

## What

Found while doing `m1-review/09`, which asked to confirm that `qwe validate`
never runs project plugin code. It does.

`plugincheck.check_lua` (`src/kernel/lua/plugincheck.lua`) loads the plugin
source with `loadstring` and then runs it:

```lua
local ok, mod = pcall(strict.run, name, chunk)
```

`strict.run` calls the chunk, so the plugin's top level executes during
validation, in the validating process, with the operator's privileges and no
sandbox. It has to: the contract check (`check_contract`) needs the module table
the chunk returns, to see that `check`/`apply` (or `argv`) are functions.
`check` and `apply` themselves are not called; those run only in a step's forked
child.

Checked on 2026-09-20 with `tests/e2e/validate_runs_plugin_top_level/`: a project
plugin whose top level writes a file has written it after `qwe validate`.

That makes `qwe validate` unsafe on a workflow directory you do not trust, which
is the one place a person would reach for a command that says "validate" rather
than "run". `CONTEXT.md` ("Trust boundary") now says so plainly. This ticket is
whether to make it true instead.

## Decision needed

- **Accept and keep documenting.** The workflow directory is trusted code
  (`m1-review/09`); validation is the same trust as running. Cost: nothing, and
  the current contract check stays exact.
- **Make validation execute nothing.** The contract check would have to work on
  source, not on the loaded module: a static look for `return { check = ...,
  apply = ... }`, or a declared contract in `schema.json` (for example
  `"kind": "check-apply" | "argv"`) that the plugin is checked against only when it
  runs. Cost: the check becomes weaker or the plugin format gains a field, and
  luacheck (which parses but does not run) is unaffected.
- **Run the top level in a child with no privileges.** Heavier than either.

## Acceptance criteria

- [ ] The decision is recorded in `CONTEXT.md` ("Trust boundary") and an ADR if the format changes.
- [ ] If validation stops executing plugin code: `tests/e2e/validate_runs_plugin_top_level/` is replaced by a case asserting the side effect does **not** happen, and `qwe validate` says nothing about it in its output.

## Comments
