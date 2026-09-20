# 05: An ssh ControlMaster outlives a killed qwe, for good

Status: resolved
Category: bug
Type: task
Blocked by: none

## What

`backend.ssh` starts a master with

```lua
{ "ssh", "-M", "-N", "-f", "-S", sock, "-o", "BatchMode=yes",
  "-o", "ConnectTimeout=10", "-o", "LogLevel=ERROR", "-E", log, host }
```

and closes it with `ssh -O exit` from `close_masters`, after `run_all` returns.
Every ordinary ending reaches that: success, failure, a step timeout, SIGINT and
SIGTERM all come back through the event loop. What does not reach it is SIGKILL,
a segfault, or anything else that ends the process without unwinding.

Checked on 2026-09-20: a run was `kill -9`ed during a step, and afterwards

```
$ pgrep -af "ssh -M -N -f"
1094617 ssh -M -N -f -S /tmp/qwe-1000/1094612.%C -o BatchMode=yes -o ConnectTimeout=10 …
```

The master is still running. There is no `ControlPersist`, so nothing ever
expires it: it holds the TCP connection and its socket until the machine
reboots or someone kills it by hand. The socket path carries the qwe pid
(m1-review of ticket 13's per-run ControlPath), so no later run reuses it
either. One leaked master and one leaked socket per target, per kill.

qwe-ssh-sec I.3 already specifies the answer and the implementation simply does
not use it: "`ssh -M -N -o ControlPersist=<ttl> …`". With `ControlPersist` the
master exits on its own once no client has used it for the ttl, so an abandoned
master is bounded instead of permanent.

**The fix.** Pass `-o ControlPersist=<ttl>`. Picking the ttl is the only real
decision: it has to be long enough that a job waiting on the session cap, or a
gap between two steps, does not lose the connection and pay a fresh handshake,
and short enough that a killed run does not leave a connection open for long.
Something on the order of a minute or two fits both. `close_masters` stays as it
is — the ttl is the backstop, not the mechanism.

Worth checking while in here: a leftover socket file from a killed run is never
cleaned up either, since the path is per-pid. Once `ControlPersist` bounds the
process, the stale socket is a dead file in the socket directory. Removing
sockets whose master is gone, at the start of a run, would keep the directory
from growing without bound on a machine where runs get killed often.

## Acceptance criteria

- [x] A run killed with SIGKILL mid-step leaves no master behind once the ttl has passed. `e2e: tests/e2e/ssh_master_expires_after_kill/` (kill the run, then poll for the master to disappear, with the ttl overridden to something short for the test the way `QWE_TEST_GRACE_MS` overrides the grace period)
- [x] Ten sequential steps still use exactly one master: the ttl does not cause a reconnect between steps. `e2e: tests/e2e/ssh_one_master_sequential/` (existing, must stay green)
- [x] Two jobs held apart by `max-sessions: 1` still share one master: the second job does not pay a new handshake. `e2e: tests/e2e/ssh_session_cap/` (existing, must stay green)
- [x] A normal end still closes the master immediately rather than leaving it for the ttl, including after SIGINT. `e2e: tests/e2e/ssh_master_closed/` (existing, must stay green)
- [x] A socket left by an earlier killed run does not accumulate: a run removes sockets in its socket directory whose master is gone. `e2e: tests/e2e/ssh_stale_socket_cleaned/`
- [x] The ttl and the reason for its value are recorded in qwe-ssh-sec I.3.

## Comments

Resolved 2026-09-20. `master_argv` passes `-o ControlPersist=120` (`QWE_TEST_MASTER_TTL` overrides it, read in `ensure`). `ensure` sweeps the socket directory once per process: an entry `<pid>.<hash>` whose qwe pid has no `/proc/<pid>` and whose master fails `ssh -O check` is removed. Both new e2e cases were mutation-checked: without ControlPersist the kill case fails after its 10 s poll, and without the sweep the stale-socket case fails. The cases use `XDG_RUNTIME_DIR=xdg` (see ticket 04) so their masters are found by socket path and never confused with another test's; the kill case expects exit 137.
