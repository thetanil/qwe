# 09: Plugin system: built-in bytecode, project plugins, strict globals, linting

Status: resolved
Type: task
Blocked by: 05

## What

- **Built-in plugins.** Build them from `plugins/builtin/<name>/{plugin.lua,schema.json}`: a Bazel rule compiles the Lua to LuaJIT bytecode and embeds it with the schema.
- **Project plugins.** Load them from source out of `.qwe/plugins/<name>/` next to the workflow, with the same layout.
- **No shadowing.** A project plugin with a built-in plugin's name is a validation error.
- **Strict globals.** All plugin code runs with strict globals.
- **Vendor luacheck** (plus argparse, plus luafilesystem or an API-only integration without directory scanning) into `third_party/`.
- **Validation.** `qwe validate` runs on every project plugin: the metaschema check on its schemas, the plugin contract check, and luacheck. Built-in plugins go through the same three checks as Bazel tests.

## Acceptance criteria

- [x] The built-in `run` plugin loads from embedded bytecode, with no `.lua` file on disk at run time. `e2e: tests/e2e/builtin_no_disk/`
- [x] A project plugin in `.qwe/plugins/hello/` is usable as `uses: hello`. `e2e: tests/e2e/project_plugin_loads/`
- [x] A project plugin named `run` (or `file.ensure`) fails validation, naming both sources. `e2e: tests/e2e/project_plugin_no_shadowing/`
- [x] A plugin that assigns an undeclared global fails its step with reason `plugin-error` and a message naming the global. `e2e: tests/e2e/strict_globals_write/`
- [x] A plugin that reads an undeclared global fails the same way. `e2e: tests/e2e/strict_globals_read/`
- [x] The **qwe strict metaschema** exists. It is draft-07's metaschema with:
  - a qwe `$id`,
  - root `additionalProperties: false`,
  - `pattern`, `patternProperties` and `format` removed,
  - `secret` added for output schemas,
  - `$schema` pinned to draft-07.

  It rejects `{"type":"object","requried":["x"]}` at `/requried` and `{"properties":{"x":{"minLenght":1}}}` at `/properties/x/minLenght`. `unit: src/kernel/schema/strict_meta_test::metaschema_rejects_typo`, `::rejects_nested_typo` (moved from ticket 02)
- [x] A property that happens to be *named* `requried` inside `properties: {}` is allowed. `unit: src/kernel/schema/strict_meta_test::property_named_like_keyword_ok`
- [x] A plugin schema that uses `pattern` is rejected, never silently ignored. `unit: src/kernel/schema/strict_meta_test::pattern_rejected`
- [x] A step union discriminated by `uses` (the `if`/`then` form) accepts each plugin's valid `with:` and rejects an invalid one. `unit: src/kernel/schema/union_test::if_then_discriminator` (replaces the `oneOf`/`const` test from ticket 02)
- [x] A project plugin whose schema contains `"requried"` fails `qwe validate` (metaschema). `e2e: tests/e2e/plugin_schema_metaschema/`
- [x] A project plugin with no `check`/`apply` and no `run`-like marker fails the contract check. `e2e: tests/e2e/plugin_contract_missing_apply/`
- [x] A project plugin that declares an output without a `secret` flag fails the contract check. `e2e: tests/e2e/plugin_contract_output_flags/`
- [x] A luacheck warning in a project plugin is reported with `file:line:col` and fails validation. `e2e: tests/e2e/plugin_luacheck/`
- [x] Every built-in plugin passes the metaschema, contract and luacheck checks. `unit: plugins/builtin:lint_test` (a Bazel test)

## Comments

### Resolution

- Built-ins live at `plugins/builtin/<name>/{plugin.lua,schema.json}` and are embedded as bytecode (`plugin.<name>`, `plugin_schema.<name>`). `run` is a reserved built-in name but not a `uses:` plugin. `package.path`/`cpath` are empty, so nothing loads from disk; `builtin_no_disk` proves it with decoys on `LUA_PATH` and in the work dir.
- `schema.json` is now `{"with": …, "outputs": …}` (`file.ensure`'s was rewrapped). Outputs must carry `"secret": true|false`.
- Registry and project loading: `src/kernel/lua/plugins.lua`. Checks (metaschema, contract, luacheck): `plugincheck.lua`. Strict globals: `strict.lua` (`_G` is the plugin's own env). `qwe.fs` (`luafs.c`) lists the plugin dir.
- `uses:` steps now run in the forked child (`qwe.plugins.run_step`), exit 125 maps to reason `plugin-error`. The engine calls `apply(with)` only; ticket 10 adds check-then-apply.
- Metaschema: `src/kernel/schema/strict-metaschema.json`; tests `strict_meta_test`, `union_test` there.
- Deviations: luacheck is vendored and used through its API only, so argparse and luafilesystem are not vendored. `file.ensure` has a stub `plugin.lua` (check/apply raise "not implemented") so it passes the contract until ticket 10. Errors inside `schema.json` are reported as `file: at /json/pointer: message` (no line numbers, per the open question in the spec).
- `bazel test //...` green (73 tests).
