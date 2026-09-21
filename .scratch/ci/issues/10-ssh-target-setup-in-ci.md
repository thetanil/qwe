# 10: The ssh target in CI: make its setup robust and documented

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: none

## What

Ticket 02 gave the runner an ssh target inside `.github/actions/setup/action.yml`
(`ssh-target: true`). It works: all 19 `needs-ssh` cases ran and passed in the first
green `tests`, `asan`, `ubsan` and `coverage` runs. It leaves loose ends, and none of
the design is written down outside the action:

- It deletes `/etc/sudoers.d/runner` (and `90-cloud-init-users`) so `ssh_become_denied`
  sees no passwordless sudo. A later step that needs `sudo` fails. Check for the
  sudoers files by name is image-specific; make it detect the rule instead
  (`sudo -n -l` as the runner user) and fail loudly if it cannot be removed.
- `172.18.0.1/32` is added to `lo`, and `known_hosts` is filled with `ssh-keyscan`.
  Confirm both are needed and that nothing depends on the runner's own hostname.
- The ssh-agent is started with `RUNNER_TRACKING_ID=""` so it outlives the step. Check
  it is gone at job end and that a second job in the same workflow (valgrind has two)
  gets its own.
- `QWE_E2E_REQUIRE_SSH=1` and `REMOTE_CONTAINERS=1` are written to `~/.bazelrc`; the
  repo `.bazelrc` also passes `QWE_E2E_REQUIRE_SSH` through. Say which wins and why.
- Add a "How CI reaches ssh" section to `docs/ci-checks.md`: what the setup assumes
  (`172.18.0.1`, no passwordless sudo, an agent), and what the fallback is
  (`QWE_E2E_SSH_HOST`) if the loopback alias ever stops working on runners.

## Acceptance criteria

- [ ] The sudoers removal keys on the effective rule, not a file name. `manual: read the action; run the job`
- [ ] `docs/ci-checks.md` documents the ssh target and its assumptions. `manual: read`
- [ ] The valgrind workflow's two jobs each get a working target. `manual: push; both jobs' ssh steps are green`

## Comments
