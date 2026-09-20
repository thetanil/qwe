# 10: Measure coverage of the Lua

Status: resolved
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

- [x] One documented command produces per-file line coverage for the Lua, over the Lua tests and the e2e cases together.
- [x] The e2e cases count: the child that runs a step is a separate process, so its coverage must be collected too.
- [x] A baseline is recorded in `docs/coverage.md`.
- [x] Each Lua file with a miss gains a test or a written reason.

## Comments

### Resolved

`bazel coverage //... --combined_report=lcov` now reports the Lua in the same file as the C; how it works, the awk to read it, the baseline and the after are in `docs/coverage.md`. `bazel test //...` is green.

**Built (TDD, one slice at a time).** `qwe.luacov` (`src/kernel/lua/luacov.lua`): a `debug.sethook` line counter, `executable_lines` (walks a loaded chunk's protos with `jit.util`, so unrun lines count as 0 rather than missing), and `lcov` text. Unit test `luacov_test` (expected lcov and line sets are literals worked by hand from the source layout). `bcembed` now records each module's source path so the hook can name files. `qwe_lua_new` enables it when `QWE_LUA_COVERAGE` and `COVERAGE_DIR` are set; `.bazelrc` sets the former for `bazel coverage` only.

- [x] One documented command: `bazel coverage //... --combined_report=lcov`, unit tests and e2e cases together.
- [x] The step child: it execs away and takes its counts with it, so `child_argv` flushes before exiting or exec'ing. `e2e: luacov_child_process` (checked by removing the flushes: it goes red with "want a file per process, got 1").
- [x] Baseline in `docs/coverage.md`: 1127/1190 Lua lines (94.7%); now 1180/1190 (99.2%).
- [x] Each file with a miss has a test or a written reason (table in `docs/coverage.md`). Nine new e2e cases cover the error paths of `template`, `validate`, `inventory`, `plugins`, `plugincheck`, `pluginshape`.

**A surprise worth knowing.** Bazel's lcov merger silently drops any source not in the instrumented-files manifest, so the first version produced correct `.dat` files that never reached the report. `lua_instrumented` (`tools/luacov/defs.bzl`) declares the Lua; a Lua file added under a new directory needs the same.

**Not fixed.** A process that exits without closing its Lua state (a signal, an early `exit`) writes no Lua coverage. The parent's `.dat` comes from `lua_close`, so those runs under-count. Acceptable for a diagnostic.
