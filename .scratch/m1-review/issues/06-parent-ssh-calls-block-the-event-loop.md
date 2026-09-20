# 06: Parent-side ssh calls block the whole event loop

Status: needs-triage
Category: bug
Type: task
Blocked by: none

## What

The kernel's design says the parent does one thing: it runs an epoll loop over
fds and signals, and never blocks. The ssh backend broke that, quietly, in
three places. All three go through `qwe.exec.run`, which forks a child and then

```c
while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
```

waits for it to exit. That is a blocking wait, and it is called from inside the
loop:

| Caller | When | Bound |
|---|---|---|
| `remote_ensure` → `ensure` | before every remote step, and again whenever a master has died | `ConnectTimeout=10`, so up to ~10 s |
| `remote_lost` → `alive` (`ssh -O check`) | on every remote step whose leader exited 255 | none |
| `remote_kill` (`ssh -S … kill`) | on SIGTERM and SIGKILL of a remote step | `ConnectTimeout=5` |
| `close_masters` → `exit` (`ssh -O exit`) | once per target at the end | none |

While any of these runs, nothing else in the run makes progress: no other job's
output is drained, no timer is read, no signal is handled. A ten-second master
start stalls every parallel job for ten seconds. Their rings are 1 MiB and
usually absorb it, but their step and job timeouts are late by however long the
stall lasted, which is precisely the guarantee `timeout-minutes` is supposed to
make.

The unbounded cases are worse than late. `ssh -O check` and `ssh -O exit` talk
to the local control socket, so they normally return at once — but if a master
is wedged rather than dead, they can block with no timeout. qwe then hangs with
no way out: SIGINT and SIGTERM are blocked and only read through the signalfd,
which the loop is not reaching. The operator's only recourse is SIGKILL, which
is the situation ticket 05 is about.

This was found by reading, not by a failing test. The stall is real and follows
from the code; what has not been measured is how bad it is in practice on a
healthy network, where a master start is fast and `-O check` is a local socket
round trip. That measurement belongs in triage, because it decides how much the
fix is worth.

## Questions for triage

- **How much does it actually cost?** Measure a parallel run against two ssh
  targets where one master start is slowed (a target behind an unreachable
  address until `ConnectTimeout` fires) and see what the other job's step
  timeout does. If the answer is "a few hundred milliseconds on a healthy
  network, and only the pathological case hurts", the cheap fix below may be
  enough for M1.
- **Cheap fix, or the real one?** The cheap fix is a timeout on every parent-side
  ssh call (`ConnectTimeout` on all of them, plus an overall wall-clock cap in
  `exec.run` so a wedged socket cannot hang the loop). That removes the hang but
  keeps the stall. The real fix is to stop blocking: start the master as a
  tracked child with its fds in the epoll set, and drive it through the same
  loop as everything else. That is the "plugins register fds in the event loop"
  primitive that qwe-ssh-sec lists as an open question and calls the one
  genuinely kernel-touching consequence of the ssh work.
- **Does the master need to start in the parent at all?** It does today because
  only the parent can close it (I.2, and a master a child opened could be
  neither seen nor closed). Is there a shape where the parent forks a
  short-lived helper for the connect, learns the outcome through the existing
  result-pipe machinery, and never waits synchronously?
- **What is the right behaviour on a stall?** If a master takes longer than some
  budget to come up, is the step `failed` with `connection-lost`, or does it
  wait? Today it waits for `ConnectTimeout` and then fails with `engine-error`.

## Acceptance criteria (draft)

- [ ] A slow master start on one target does not delay a step timeout on another target by more than a small budget. `e2e: tests/e2e/ssh_slow_master_does_not_stall/`
- [ ] No parent-side ssh call can block forever: each has a bound, and exceeding it fails the step rather than hanging the run. `e2e: tests/e2e/ssh_wedged_socket_does_not_hang/`
- [ ] An operator's SIGINT is honoured while a master is being started. `e2e: tests/e2e/ssh_cancel_during_master_start/`
- [ ] Every existing ssh case stays green.

## Comments
