# 01: Contain each step in its own cgroup

Status: needs-triage
Type: task
Blocked by: m1-engine/17

## What

In M1 a step's processes are tracked by process group: teardown signals the group, and a step is over when its group is empty (decided while grilling m1-engine ticket 17). A process that leaves the group escapes both. `setsid`, `setpgid` into a new group, or a daemonising double fork is enough. Such a process can outlive its step, its job and qwe itself, which breaks the "no processes left behind" claim that spec Behaviour 4 makes. M1 accepts this gap on purpose. It is recorded in `docs/workflow-kernel-design.md` §9.3.

Closing it needs a cgroup v2 per step. Membership is inherited and cannot be left by `setsid`. `cgroup.kill` stops the whole tree at once, and `cgroup.events` `populated 0` is a pollable "step is empty" event that can replace the group-empty check.

## Questions for triage

- **Delegation:** qwe needs a writable cgroup subtree. Where does that come from in each setting: a systemd `Delegate=yes` unit, a devcontainer (it currently sees cgroup2 at `/` as `0::/`), or `systemd-run --user --scope`? What should qwe do when it has none: refuse, warn and fall back to process groups, or something else?
- **ssh targets:** the remote side of a step is a process tree on another host. Is containing it remotely (for example with `systemd-run --scope` over ssh) in scope, or does this ticket cover only `local`?
- **Grace period:** SIGTERM is sent to the cgroup's processes, and after the grace period `cgroup.kill` is written. Does the lifecycle table from ticket 17 need a new event, or does `populated 0` just replace `group-empty`?

## Acceptance criteria (draft)

- [ ] A step that runs `setsid sleep 1000 &` and exits 0 leaves no process behind, and qwe exits 0 only once the process is gone. `e2e: tests/e2e/setsid_escape_contained/`
- [ ] A teardown of a step whose child has escaped with `setsid` and traps SIGTERM still honours the full grace period, and then kills it. `e2e: tests/e2e/setsid_escape_teardown/`
- [ ] Without a delegated cgroup subtree, qwe behaves as decided at triage, and says so. `e2e: tests/e2e/no_cgroup_delegation/`

## Comments
