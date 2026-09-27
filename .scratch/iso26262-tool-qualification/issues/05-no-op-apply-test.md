# 05: Every plugin's check is proven honest by a no-op-apply test

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 03, 04

## What

Tickets 03 and 04 make the kernel's side of check → apply → check sound. The
plugin's side is ADR-0015's plugin requirement: check observes the target's
actual state through `ctx.backend`, reads nothing apply produced, and changes
nothing. qwe cannot prove that from the source. It can prove it by behaviour, the
same way for every plugin:

- **No-op apply:** on a target that needs a change, replace apply with a
  function that does nothing. The step must fail `not-converged`. A check that
  passes anyway is not looking at the target.
- **Check changes nothing:** run check twice in a row on an unchanged target.
  Both verdicts and outputs are equal, and the recording backend shows no command
  that the plugin's test marks as mutating (each plugin's fixture names the
  commands its apply uses; none of them may appear in a check-only run).

## Fix

- **Observer plugins.** `assert` and `file.read` have an empty `apply` and a check
  that never reports a needed change (`assert` raises on a mismatch). No-op apply
  means nothing for them. The harness recognises an observer by its fixture
  saying so, and checks the observer property instead: across every case in its
  `test.lua`, check never returns `true`, and apply issues no backend command. A
  plugin that claims to be an observer and is not fails.
- A generic harness, `plugins/builtin/convergence_test.lua`, that enumerates every
  built-in plugin with `check`/`apply` and, for each non-observer, takes a
  "needs change" fixture from the plugin's own `test.lua` (one exported table
  per plugin), and runs both properties against the recording backend. A
  plugin with no fixture fails the harness, so a new plugin cannot skip it.
- The same harness usable on a **project plugin**, because passing it is part of
  developing any plugin (ADR-0015), although project plugins are not rated: a
  documented way for a plugin author to run it (a `qwe` test entry point or a
  `luarun` script; choose the smaller one and say why in the Comments). The tool
  safety manual (ticket 02) points to it.
- When the device plugins (control, configuration, flashing) are written, their
  checks meet this through the harness like any other plugin. ADR-0015 lists
  what their checks read (the booted image identity, `boot_id`, configuration
  read back). Their own tickets link here.

## Acceptance criteria

- [ ] The harness runs both properties on every built-in check/apply plugin
      (`file.ensure`, `file.line`, `apt.package`; the observer property on
      `assert` and `file.read`; and any added since). `plugin: plugins/builtin/convergence_test.lua`
- [ ] A throwaway plugin whose check reads a flag apply set fails the harness,
      and so does one whose check runs a mutating command.
      `plugin: the harness's own negative cases`
- [ ] A new built-in plugin without a fixture fails the harness. `plugin: same`
- [ ] A project plugin can be run through the harness by a documented command.
      `e2e: tests/e2e/<case>/`
- [ ] `bazel test //...` is green.

## Comments
