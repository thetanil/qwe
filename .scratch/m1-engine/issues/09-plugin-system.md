# 09: Plugin system: built-in bytecode, project plugins, strict globals, linting

Status: ready-for-agent
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

- [ ] The built-in `run` plugin loads from embedded bytecode, with no `.lua` file on disk at run time. `e2e: tests/e2e/builtin_no_disk/`
- [ ] A project plugin in `.qwe/plugins/hello/` is usable as `uses: hello`. `e2e: tests/e2e/project_plugin_loads/`
- [ ] A project plugin named `run` (or `file.ensure`) fails validation, naming both sources. `e2e: tests/e2e/project_plugin_no_shadowing/`
- [ ] A plugin that assigns an undeclared global fails its step with reason `plugin-error` and a message naming the global. `e2e: tests/e2e/strict_globals_write/`
- [ ] A plugin that reads an undeclared global fails the same way. `e2e: tests/e2e/strict_globals_read/`
- [ ] The **qwe strict metaschema** exists. It is draft-07's metaschema with:
  - a qwe `$id`,
  - root `additionalProperties: false`,
  - `pattern`, `patternProperties` and `format` removed,
  - `secret` added for output schemas,
  - `$schema` pinned to draft-07.

  It rejects `{"type":"object","requried":["x"]}` at `/requried` and `{"properties":{"x":{"minLenght":1}}}` at `/properties/x/minLenght`. `unit: src/kernel/schema/strict_meta_test::metaschema_rejects_typo`, `::rejects_nested_typo` (moved from ticket 02)
- [ ] A property that happens to be *named* `requried` inside `properties: {}` is allowed. `unit: src/kernel/schema/strict_meta_test::property_named_like_keyword_ok`
- [ ] A plugin schema that uses `pattern` is rejected, never silently ignored. `unit: src/kernel/schema/strict_meta_test::pattern_rejected`
- [ ] A step union discriminated by `uses` (the `if`/`then` form) accepts each plugin's valid `with:` and rejects an invalid one. `unit: src/kernel/schema/union_test::if_then_discriminator` (replaces the `oneOf`/`const` test from ticket 02)
- [ ] A project plugin whose schema contains `"requried"` fails `qwe validate` (metaschema). `e2e: tests/e2e/plugin_schema_metaschema/`
- [ ] A project plugin with no `check`/`apply` and no `run`-like marker fails the contract check. `e2e: tests/e2e/plugin_contract_missing_apply/`
- [ ] A project plugin that declares an output without a `secret` flag fails the contract check. `e2e: tests/e2e/plugin_contract_output_flags/`
- [ ] A luacheck warning in a project plugin is reported with `file:line:col` and fails validation. `e2e: tests/e2e/plugin_luacheck/`
- [ ] Every built-in plugin passes the metaschema, contract and luacheck checks. `unit: plugins/builtin:lint_test` (a Bazel test)
