# 14: `become`

Status: ready-for-agent
Type: task
Blocked by: 13

## What

Translate a step's `become:` value:

| Value | Translation |
|---|---|
| absent / `false` | nothing added |
| `true` | `sudo -n` |
| a string | `sudo -n -u <name>` |
| an integer | `sudo -n -u '#<uid>'` |

`sudo` wraps the shell that reads the preamble, so declared env survives `env_reset`. If sudo refuses, the step is `failed` with reason `become-denied`, immediately and without hanging.

The host `172.18.0.1` (`zeta`) has **no** passwordless sudo, which the denial test relies on. The success path for `become` is proven with the recording backend, since there's no host with passwordless sudo available to the tests.

## Acceptance criteria

- [ ] Translation of `true`, `"runner"`, `1001`, `0`, `false` and absent gives the exact argv. `0` means uid 0 and is distinct from `false`. `plugin: plugins/builtin/backend-ssh/test.lua::become_translation`
- [ ] `sudo` wraps the preamble shell, so the env reader runs inside sudo (argv shape asserted). `plugin: plugins/builtin/backend-ssh/test.lua::become_wraps_preamble`
- [ ] `become: true` on `self` (zeta) is `failed` with reason `become-denied` within 5 seconds and doesn't hang waiting for a password prompt. `e2e: tests/e2e/ssh_become_denied/`
- [ ] `become: true` on `local` inside the devcontainer resolves according to the local sudo configuration, and denial reports `become-denied`. `e2e: tests/e2e/local_become/`
- [ ] A `become` value of the wrong type (a list, for example) is a validation error with a position. `e2e: tests/e2e/validate_become_type/`
