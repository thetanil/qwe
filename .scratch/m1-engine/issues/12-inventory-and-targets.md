# 12: Inventory reader and target resolution

Status: ready-for-agent
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

- [ ] With no `-i`, `inventory.yaml` next to the workflow is used. `e2e: tests/e2e/inventory_default_location/`
- [ ] `-i` takes precedence over the default location. `e2e: tests/e2e/inventory_flag_wins/`
- [ ] `target: nope` is a validation error at the `target:` position. `e2e: tests/e2e/unknown_target/`
- [ ] A workflow that uses only `local` needs no inventory. `e2e: tests/e2e/local_needs_no_inventory/`
- [ ] Defining a target named `local` in the inventory is an error. `e2e: tests/e2e/inventory_local_reserved/`
- [ ] An unknown `backend:` value is a validation error. `e2e: tests/e2e/inventory_unknown_backend/`
- [ ] A `backend: ssh` target without `host:` is a validation error at the target's position. `e2e: tests/e2e/inventory_ssh_requires_host/`
- [ ] `${{ vars.arch }}` resolves to the job's target's value, and the same workflow run with two inventories produces two different values. `e2e: tests/e2e/vars_per_target/`
- [ ] `${{ vars.nope }}` is a validation error at its position. `e2e: tests/e2e/vars_unknown_key/`
- [ ] Target vars aren't in a step's environment unless mapped with `env:`. `e2e: tests/e2e/vars_not_auto_exported/`
- [ ] `vars` may be substituted into `run:` text. `e2e: tests/e2e/vars_in_run_text/`
- [ ] An `!encrypted` value outside a `secrets:` map (for example in `vars:`) is a validation error. `e2e: tests/e2e/envelope_only_in_secrets/`
- [ ] A `groups:` or `defaults:` key is rejected as unknown. `e2e: tests/e2e/inventory_no_inheritance_keys/`
