# 12: Inventory reader and target resolution

Status: resolved
Type: task
Blocked by: 05

## What

- **Inventory reader.** A service plugin that reads the inventory: `-i`, otherwise `inventory.yaml` in the workflow's directory. It holds:
  - an inventory-wide `secrets:` map,
  - a `targets:` map. Each target is a discriminated union on `backend`: `ssh` requires `host:` (an ssh_config Host name or address). Optional fields: `max-sessions`, `become-password` (reserved), `vars:` and `secrets:`.
- **No inheritance.** There are no group, defaults or var-cascade keys, and any such key is rejected as unknown.
- **`local`** is implicit in the inventory, can't be redefined, and has no vars or secrets. It is **not** a default: every job must declare `target:`.
- **Validation** resolves every job's `target:` to a backend.
- **The `${{ vars.X }}` context** resolves against the job's target.

## Acceptance criteria

- [x] With no `-i`, `inventory.yaml` next to the workflow is used. `e2e: tests/e2e/inventory_default_location/`
- [x] `-i` takes precedence over the default location. `e2e: tests/e2e/inventory_flag_wins/`
- [x] `target: nope` is a validation error at the `target:` position. `e2e: tests/e2e/unknown_target/`
- [x] A workflow that uses only `local` needs no inventory. `e2e: tests/e2e/local_needs_no_inventory/`
- [x] Defining a target named `local` in the inventory is an error. `e2e: tests/e2e/inventory_local_reserved/`
- [x] An unknown `backend:` value is a validation error. `e2e: tests/e2e/inventory_unknown_backend/`
- [x] A `backend: ssh` target without `host:` is a validation error at the target's position. `e2e: tests/e2e/inventory_ssh_requires_host/`
- [x] `${{ vars.arch }}` resolves to the job's target's value, and the same workflow run with two inventories produces two different values. `e2e: tests/e2e/vars_per_target/`
- [x] `${{ vars.nope }}` is a validation error at its position. `e2e: tests/e2e/vars_unknown_key/`
- [x] Target vars aren't in a step's environment unless mapped with `env:`. `e2e: tests/e2e/vars_not_auto_exported/`
- [x] `vars` may be substituted into `run:` text. `e2e: tests/e2e/vars_in_run_text/`
- [x] An `!encrypted` value outside a `secrets:` map (for example in `vars:`) is a validation error. `e2e: tests/e2e/envelope_only_in_secrets/`
- [x] A `groups:` or `defaults:` key is rejected as unknown. `e2e: tests/e2e/inventory_no_inheritance_keys/`

## Comments

- **Inventory reader.** `src/kernel/lua/inventory.lua` (`qwe.inventory`) with `inventory.schema.json`: schema check via `qwe.validate.check`, plus `local` reserved, var-name check, and the `!encrypted`-only-in-`secrets:` scan. C loads it in `load_inventory` (`workflow.c`) before the workflow, prints errors as `inventory.yaml:line:col`, then `use()` makes it current; `validate` and `template` read it from there. `-i` is parsed by `qwe run` and `qwe validate`; a missing default file means no inventory, a missing `-i` file is an error.
- **Decisions the ticket left open.**
  - `backend:` accepts only `ssh` for now. `become-password` accepts only a plain string (reserved), so an `!encrypted` there is rejected like any envelope outside `secrets:`.
  - `${{ vars.X }}` is allowed in run text, `with:`, and job/step `env:` values. It is rejected in the workflow-level `env:` (vars belong to a job's target). `local` has no vars, so any `vars.X` in a `local` job is an error.
  - Inventory errors stop before the workflow is validated.
- **No ssh backend yet (ticket 13).** To make `vars` testable at run time, `on` is no longer refused, and a job whose target isn't `local` is refused unless every step says `on: local`. Those steps run on the operator host, as `on: local` means anyway.
- `run_case.sh` exports `$QWE_BIN` to `check.sh` so a case can run qwe again (`vars_per_target` runs the same workflow against a second inventory).
- `validate_unknown_context` now uses `matrix.os`, since `vars` is a known context.
- `bazel test //...` green (105 tests).
