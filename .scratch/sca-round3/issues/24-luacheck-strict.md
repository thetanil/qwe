# 24: luacheck, strict

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 23

## What

luacheck's defaults are a floor. `plugincheck.lua` calls it with
`{ std = "luajit", max_line_length = false }`: line length is switched off, an
exception for user plugins that is right for user code and does not need to be
right for ours. Settings that matter for a safety case:

- **Cyclomatic complexity** (`max_cyclomatic_complexity`): off by default. The
  Lua has no analogue of the C ticket 22, and `template.lua` is 329 lines and
  `validate.lua` 277.
- **Line length** (`max_line_length`) is on by default (120). Keep it on, and
  decide whether 120 is the number: it is the checker's, not ours.
- **Unused variables/arguments/values** and shadowing are on by default; keep
  every one on (`unused_args`, `unused_secondaries`, `redefined`, `self`).
- `allow_defined = false` and `allow_defined_top = false` (an assignment to an
  undeclared name is an error, not a definition) are the defaults; do not relax.
- **`strict.lua`** already enforces undefined-global access at runtime; luacheck's
  global checks are its static counterpart. Say in the Comments whether they agree
  on all files.

`plugincheck.lua`'s own call (for *user* plugins) is product behaviour, not
part of this ticket. If the repo wants the same strictness on user plugins,
that is a product decision for its own ticket, outside this feature.

## Fix

Add the options to `.luacheckrc` for the ticket-23 test only. Fix every finding
(split functions, shorten lines). No `-- luacheck: ignore` comments and no
`ignore = {}` entries: a suppression is an exception.

## Acceptance criteria

- [ ] `.luacheckrc` sets `max_cyclomatic_complexity` (start at 10, the common
      figure; use a higher number only if the ticket records a specific function
      and why it cannot be split), `max_line_length`, and no `ignore`.
      `manual: read .luacheckrc`
- [ ] `//tools/lua:luacheck_test` is green, with no `-- luacheck:` comment in any
      `.lua` outside `third_party/`. `manual: grep -rn "luacheck:" src plugins tools tests`
- [ ] The Lua tests are unchanged in count and outcome. `unit: the Lua tests`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
