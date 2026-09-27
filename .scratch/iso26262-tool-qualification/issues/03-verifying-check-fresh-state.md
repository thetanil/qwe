# 03: The verifying check shares nothing with apply but the target

Status: ready-for-agent
Category: bug
Type: task

## What

ADR-0015 makes the kernel's check → apply → check a tool requirement and the
core of what is validated: the second check has to tell the truth about the
**target**. Today it
cannot be relied on to. `run_step` in `src/kernel/lua/plugins.lua` opens the
plugin module once, builds one `with` table and one `ctx`, and
`qwe.checkapply.run` passes the same three to check, apply and the verifying
check. So apply can leave state behind that makes the second check pass without
looking at the target:

- a module-level `local` (an upvalue) that apply sets and check reads;
- a field apply writes into `with` (for example, the desired value overwritten
  with the actual one, so the comparison agrees);
- something cached on `ctx` or on the backend object.

A plugin may do this by accident as easily as on purpose ("remember what I just
did"), and the step then reports success for a change that never happened. That
is exactly the malfunction the verifying check exists to catch. Until this is
fixed it is a known malfunction in the tool safety manual.

## Fix

Give the verifying check fresh state:

- the plugin module loaded again, not taken from a cache (`open` must bypass
  `package.loaded` or the registry for this load);
- a deep copy of the step's original `with`, taken before the first check ran
  (the first check and apply get their own copies too, so neither can change
  what the other sees);
- a new `ctx` with a new backend object. Reusing an underlying connection, such
  as an ssh control socket, is fine as long as no Lua-visible state carries over.

Running the verifying check in a fresh Lua state (a second forked child) is the
stronger form. Choose it if the in-process form cannot be made airtight, and
say which in the Comments. Keep `changed`, the outputs (taken from the verifying
check, as today) and the result-pipe protocol unchanged.

## Acceptance criteria

- [ ] Red first: a test plugin whose apply does nothing to the target but sets a
      module upvalue that check reads fails `not-converged`.
      `unit: src/kernel/lua/checkapply_test.lua` (or the file that tests
      `run_step`)
- [ ] The same for apply writing into `with`, and for apply caching a value on
      `ctx`/`ctx.backend`. `unit: same file`
- [ ] A plugin whose apply really changes the (recorded or real) target still
      converges with `changed = true`; the existing plugin tests and
      `tests/e2e/not_converged` are unchanged. `unit` `e2e: tests/e2e/not_converged/`
- [ ] Design §12.3 and CONTEXT.md's "apply" entry say the verifying check runs
      on fresh state. `manual: both docs`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
