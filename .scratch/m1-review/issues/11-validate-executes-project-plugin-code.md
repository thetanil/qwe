# 11: `qwe validate` executes a project plugin's top level

Status: resolved
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

- [x] The decision is recorded in `CONTEXT.md` ("Trust boundary") and an ADR if the format changes.
- [x] If validation stops executing plugin code: `tests/e2e/validate_runs_plugin_top_level/` is replaced by a case asserting the side effect does **not** happen, and `qwe validate` says nothing about it in its output.

## Comments

Resolved 2026-09-20, by the second option, on the operator's decision that validate must not run plugin code.

`schema.json` gains a required `kind` (`check-apply` | `argv`). `plugincheck.check` now does luacheck, compile-only `loadstring`, and the schema/kind check; nothing is executed. The contract check moved to `plugins.run_step`, which checks the loaded module against its kind before use and fails the step with `plugin-error` (`plugins.contract_problem`). Built-in plugins declare a kind too, and `lint_test` uses `plugincheck.check_builtin`, which does load them (our own code) and checks the contract there. Cases: `validate_runs_plugin_top_level` became `validate_runs_no_plugin_code` (no file is written by validate); `plugin_contract_missing_apply` is now a `run` case (validate passes, the step fails `plugin-error`); new `plugin_kind_required`. All 14 project-plugin fixtures gained `"kind": "check-apply"`. Cost accepted: a plugin missing `apply` is found at run time, not in `validate`.
