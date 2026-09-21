# 10: The ssh target in CI: make its setup robust and documented

Status: resolved
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

- [x] The sudoers removal keys on the effective rule, not a file name. `manual: read the action; run the job`
- [x] `docs/ci-checks.md` documents the ssh target and its assumptions. `manual: read`
- [x] The valgrind workflow's two jobs each get a working target. `manual: push; both jobs' ssh steps are green`

## Comments

- The sudoers step now checks `sudo -n true`, removes the `/etc/sudoers.d` files that grep finds a `NOPASSWD` line in, and fails the job (printing `sudo -n -l`) if `sudo -n` still works. Untested until a run: whether the runner's rule always lives in `sudoers.d`.
- Findings, from reading and reasoning, not from a run: the `lo` alias is needed because the cases hardcode `172.18.0.1`; `known_hosts` is needed because the cases use `BatchMode=yes`; each job has its own VM, so the agent cannot leak between the two valgrind jobs. There is no `QWE_E2E_SSH_HOST` variable in the tests (the ticket assumed one), so the docs say there is no coded fallback.
- `REMOTE_CONTAINERS` and `QWE_E2E_REQUIRE_SSH` go through `$GITHUB_ENV`, not `~/.bazelrc` (the comment in the action was stale); the `.bazelrc` `test_env` lines only forward them.
- Run of 7b61d5d (tests, asan, ubsan, coverage green; `valgrind.yml` dispatched, run 35663134677, both jobs green): the setup removed `/etc/sudoers.d/90-cloud-init-users` and `/etc/sudoers.d/runner` by their `NOPASSWD` lines in the tests run and in both valgrind jobs, and the sudo checks passed. `ssh_become_denied_test` PASSED (not cached) in the valgrind unit job.
