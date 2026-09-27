# 04: check's verdict is a boolean, and nothing skips the verifying check

Status: ready-for-agent
Category: bug
Type: task

## What

`src/kernel/lua/checkapply.lua`:

```lua
local needs, outputs = mod.check(with, ctx)
if not needs then
  return { status = "ok", changed = false, outputs = outputs or {} }
end
```

A check that returns nothing, because of a missing `return`, a branch that falls
off the end, or a `return nil, outputs`, reads as "no change needed". The step
reports `success`, `changed = false`, and apply never runs. After apply, the same
`nil` reads as "converged". A buggy check is therefore a silent pass on both
sides of apply. That breaks a tool requirement of ADR-0015, and it is a known
malfunction until this is fixed.

The ADR also requires that nothing can switch the verifying check off. Today
nothing does, but nothing pins that either.

## Fix

- check must return `true` or `false` as its first value. Anything else (`nil`,
  a number, a string, a table) is `plugin-error`, with a stderr message naming
  the plugin and the type it returned. The same rule applies to the verifying
  check.
- `outputs`, when present, must be a table. Anything else is `plugin-error`.
- `pluginshape`/luacheck cannot prove the return type statically; this is a
  runtime rule. Say so in design §12.3.
- A test pins that no workflow key, CLI flag or environment variable skips the
  verifying check: it lists the keys the schema accepts on a `uses:` step and
  the `qwe run` flags, and fails if a new one is added without a line in the test
  saying it does not affect check → apply → check. When the deferred `--check`
  mode arrives, it reports a needed change as `needs change`, never as success.

## Acceptance criteria

- [ ] Red first: a check returning `nil`, `1`, `"no"` or `{}` (before apply, and
      after apply) fails the step with `plugin-error`.
      `unit: src/kernel/lua/checkapply_test.lua`
- [ ] Non-table `outputs` is `plugin-error`. `unit: same file`
- [ ] Every built-in plugin's check returns a boolean in all its tests.
      `plugin: plugins/builtin/*/test.lua` (unchanged, still green)
- [ ] The no-skip test exists. `unit: src/kernel/lua/checkapply_test.lua` or a
      workflow-schema test
- [ ] Design §12.3 and CONTEXT.md state the rule. `manual: both docs`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
