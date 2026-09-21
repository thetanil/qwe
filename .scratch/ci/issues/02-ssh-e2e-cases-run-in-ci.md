# 02: The ssh e2e cases run in CI instead of skipping

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: 01

## What

19 e2e cases have a `needs-ssh` marker. `tests/e2e/run_case.sh` skips each one
(prints SKIP, passes) unless `REMOTE_CONTAINERS` is set and
`ssh -o BatchMode=yes 172.18.0.1 true` works. `172.18.0.1` is hardcoded in the
cases' `inventory.yaml` and `check.sh`/`setup.sh` files. On a GitHub runner they
would all skip, and that causes two problems:

- The ssh backend, the ControlMaster handling, `become` and the remote teardown
  would never be tested in CI. That is most of what M1 found wrong in review.
- The coverage floor (`tools/coverage/floor.txt`) was measured with these cases
  running. Without them, `src/` files for the ssh backend show more misses, and
  ticket 06's coverage workflow fails.

Give the runner a real ssh target, in the setup action behind an input
(`ssh-target: true`) that ticket 01's action gains:

- Start `sshd` on the runner (`openssh-server` is on the image), on a loopback
  address. Generate a throwaway key, add it to an `ssh-agent`, authorize it for
  the runner user, and export `SSH_AUTH_SOCK` for later steps. `.bazelrc` already
  passes `SSH_AUTH_SOCK`, `HOME` and `REMOTE_CONTAINERS` through to tests.
- Make `172.18.0.1` reachable. **Preferred:** add it as an address on `lo`
  (`sudo ip addr add 172.18.0.1/32 dev lo`) and pre-seed `known_hosts`, so no test
  file changes and the devcontainer keeps working as it is. If that turns out not
  to work on a runner, the fallback is an env var (for example `QWE_E2E_SSH_HOST`,
  default `172.18.0.1`) that `run_case.sh` substitutes into the case's copied
  files. Record which one was used, and why, under Comments.
- Set `REMOTE_CONTAINERS=1` for the test step.
- Whatever the `ssh_become_*` cases need from the target user's sudo rules
  (passwordless for the allowed case, denied for `ssh_become_denied`) is set up
  the way the devcontainer's host has it. Read those cases' `setup.sh` and
  inventories first, and write down what they assume.

Turn it on in `tests.yml`. Later gate workflows use `ssh-target: true` too.

**A skip must not look like a pass in CI.** Add a `QWE_E2E_REQUIRE_SSH=1` mode to
`run_case.sh`: when it is set, a `needs-ssh` case that cannot reach its target
fails instead of printing SKIP. The CI workflows set it. The devcontainer does
not, so local runs without the host still skip.

## Acceptance criteria

- [x] With `QWE_E2E_REQUIRE_SSH=1` and no reachable target, a `needs-ssh` case fails with a message naming the case and the host. `unit: tests/e2e/require_ssh_test.sh`
- [x] Without it, the same case still prints SKIP and passes, which is today's behaviour. `unit: tests/e2e/require_ssh_test.sh` (second invocation in the case's `check.sh`, or a sibling case)
- [x] In the `tests.yml` run, all 19 `needs-ssh` cases run, and none prints SKIP. `manual: push; grep the run's test logs for "SKIP:"; expect none`
- [x] Every ssh case passes on the runner. `manual: the same run is green`
- [ ] The devcontainer is unaffected: `bazel test //...` there is still green with the cases running against the real host. `manual: run locally before committing`

## Comments

- Implemented as `//tests/e2e:require_ssh_test` (an sh_test with a fake failing `ssh`), not an e2e case dir: a case runs under the real environment, where the target may be reachable.
- Approach used: the preferred one, `172.18.0.1/32` added to `lo`, `known_hosts` pre-seeded by ssh-keyscan; no case files changed. Whether it works on a runner is checked by the first run.
- The `ssh_become_*` assumption: only `ssh_become_denied` uses sudo, and it expects no passwordless sudo for the ssh user (zeta's). The runner user has it, so the setup action deletes `/etc/sudoers.d/runner` last and checks `sudo -n true` fails over ssh.
- `.bazelrc` passes `QWE_E2E_REQUIRE_SSH` through; the action's `~/.bazelrc` sets it and `REMOTE_CONTAINERS=1` to 1.
