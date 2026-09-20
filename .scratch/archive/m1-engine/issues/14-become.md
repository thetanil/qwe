# 14: `become`

Status: resolved
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

- [x] Translation of `true`, `"runner"`, `1001`, `0`, `false` and absent gives the exact argv. `0` means uid 0 and is distinct from `false`. `plugin: plugins/builtin/backend-ssh/test.lua::become_translation`
- [x] `sudo` wraps the preamble shell, so the env reader runs inside sudo (argv shape asserted). `plugin: plugins/builtin/backend-ssh/test.lua::become_wraps_preamble`
- [x] `become: true` on `self` (zeta) is `failed` with reason `become-denied` within 5 seconds and doesn't hang waiting for a password prompt. `e2e: tests/e2e/ssh_become_denied/`
- [x] `become: true` on `local` inside the devcontainer resolves according to the local sudo configuration, and denial reports `become-denied`. `e2e: tests/e2e/local_become/`
- [x] A `become` value of the wrong type (a list, for example) is a validation error with a position. `e2e: tests/e2e/validate_become_type/`

## Comments

- **Where it lives.** `src/kernel/lua/become.lua` (`qwe.become`): `prefix(value)` is the one translation both backends use, and `check(backend)` is the refusal probe. `backend.local` puts the prefix in front of `sh -c <bootstrap>`; `backend.ssh` puts it in front of the same words inside the remote command string (`sudo -n -u '#1001' sh -c '...' sh '<script>'`), so sudo wraps the preamble reader.
- **How `become-denied` is told apart.** sudo exits 1 when it refuses, which a command can also exit with, so the exit code cannot say. Instead `qwe.plugins.run_step` runs `true` through the same backend first (`become.check`); a non-zero result is a refusal (except ssh's own 255, which stays `connection-lost`). The child reports `{status = failed, reason = become-denied, changed = false}` on the result pipe, and `leader_exit_event` now reads that message for `run:` steps too. Nothing of the step runs after a refusal. Costs one extra sudo (and ssh session) per `become:` step.
- **Validation.** A wrong type is `become: must be true (root), a user name or an integer uid`, at the value. A string must be a plain user name (so it cannot be read as a sudo option), and a uid cannot be negative.
- **Checked by hand.** `zeta` refuses in 0.4 s with `become-denied`; local sudo here is passwordless, so `local_become` takes its success branch (uid 0, env kept) and asserts `become-denied` only where `sudo -n true` fails. On the real controller (`user@192.168.178.80`, pi00, host name `pi00`), `become: true` gave uid 0. Only `id` and `hostname` were run there, and it is not part of the test suite.
- `bazel test //...` green (119 tests).
