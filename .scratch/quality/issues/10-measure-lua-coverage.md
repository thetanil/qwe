# 10: Measure coverage of the Lua

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: none

## What

`bazel coverage` reports C only. The Lua that qwe ships (`src/kernel/lua/*.lua`:
validate, template, inventory, become, plugincheck, pluginshape, checkapply;
`plugins/builtin/**`: run, file.ensure, backend_local, backend-ssh, recording)
has no coverage data at all, so the plugin half of the engine is not measured.

Vendor a line-coverage hook (LuaJIT's `debug.sethook` is enough; luacov is the
usual choice but must be vendored, no network at build time, no Python), turn it
on for the Lua test targets and for the e2e cases, and merge the result into the
same per-file report, or produce a second one with the same command shape.

Then do what 05 did for C: read the misses, turn the error paths into tests, and
dismiss the rest in writing.

## Acceptance criteria

- [ ] One documented command produces per-file line coverage for the Lua, over the Lua tests and the e2e cases together.
- [ ] The e2e cases count: the child that runs a step is a separate process, so its coverage must be collected too.
- [ ] A baseline is recorded in `docs/coverage.md`.
- [ ] Each Lua file with a miss gains a test or a written reason.

## Comments
