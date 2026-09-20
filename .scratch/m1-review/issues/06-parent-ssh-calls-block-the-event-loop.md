# 06: Parent-side ssh calls block the event loop

Status: resolved
Category: bug
Type: task
Blocked by: none

## What

The kernel's design says the parent does one thing: it runs an epoll loop over
fds and signals, and never blocks. The ssh backend broke that. Four calls go
through `qwe.exec.run`, which forks and then `waitpid(pid, …, 0)` — a blocking
wait — from inside the loop:

| Caller | When | Bound today |
|---|---|---|
| `remote_ensure` → `ensure` | before every remote step; starts a master if none is up | `ConnectTimeout=10` |
| `remote_ensure` → `alive` (`ssh -O check`) | before every remote step | **none** |
| `remote_kill` | on TERM and KILL of a remote step | `ConnectTimeout=5` |
| `close_masters` (`ssh -O exit`) | once per target at the end | **none** |

### What it costs, measured

Measured 2026-09-20 against `172.18.0.1`:

| Path | Cost |
|---|---|
| Master start, healthy host | **306 ms**, once per target |
| `ssh -O check` | **2–3 ms**, before every remote step |
| A remote command, for scale | 46 ms |
| Master start, unreachable host (`192.0.2.1`) | **10 s** |
| A step whose master was never started | fails **3–4 ms**, exit 255, no connection attempted |

The healthy path is a non-issue: 3 ms of stall against a 46 ms remote command.
Nobody would notice it and it is not worth engineering for.

**The failure path is the whole ticket.** A job with a **1.0 s** step timeout,
already running, had its step killed at **10 s** because an unrelated job was
connecting to an unreachable host. One bad entry in an inventory multiplies
every other job's timeout by the connect timeout.

For contrast, the loop itself is already excellent. A 1.000 s step timeout
fired at **0.996138 s** idle, and at **0.996134 s** while another job pushed
40 MB through the ring. Every millisecond of slop comes from the blocking ssh
calls and nothing else.

### The guarantee

A declared timeout is **exact**: a step or job timeout fires **within 100 ms**
of its deadline, whatever else the run is doing. That is the property this
ticket restores, and it is testable against the microsecond timestamps in
`lifecycle.trace`. (The key that declares it is being renamed from
`timeout-minutes` to `timeout-seconds` under a separate ticket; this one is
about the guarantee, not the spelling.)

### The design

**Pre-connect, always.** Before the first job starts, open a master to each
distinct target used by the **selected** jobs (after `--job` filtering), one
after another. No timer is armed during this phase, so a slow host costs
startup time and cannot make anything late. Serial is deliberate: 306 ms per
target means five controllers cost ~1.5 s, and doing it in parallel means
tracking N in-flight children before the event loop exists. Parallelise later
if a lab gets big enough to care.

**No `--no-preconnect` flag.** It would cost about three lines — the lazy path
is exactly today's `ensure()` — but it would make the timeout guarantee
optional. Two guarantee levels means every test of the exact property has to
say which mode it ran in, and the safety argument acquires a sentence saying
the bound does not hold when the flag is passed. Since pre-connect only
contacts targets the selected jobs actually use, a broken host that this run
does not touch is never contacted, which removes the reason to want the flag.

**A target that cannot be pre-connected fails only its own jobs.** The rest of
the run proceeds. Jobs that `needs:` a failed one skip with
`dependency-failed` through the normal scheduler pass. A partly reachable lab
still gets useful work done, which for a hardware lab is the common case.

**The reason is `unreachable`, and there is no lifecycle table change.** Do not
route this through `start-failed`, whose reason is the table's fixed
`engine-error`. A datacenter host being down is a fact about the world, not a
fault in the engine. Instead: when pre-connect failed for a target, skip
`remote_ensure` at spawn time and let the step spawn normally. Its `ssh -S`
finds no socket and exits 255 in 3–4 ms without attempting a connection
(`-o ProxyCommand=false` kills the fallback), and the parent sets
`pl->reason = "unreachable"` on `leader-exit-fail` — the same `R_EVENT` payload
path that already carries `exit-code` and `connection-lost`. One doomed spawn
per job, since a failed step ends the job. This *narrows* `engine-error` to
what it is for: what remains on the start-failed path is ring, log and timer
allocation failures and a failed `fork`.

Three situations, three honest reasons:

| Situation | Reason |
|---|---|
| Never reached it: pre-connect failed | `unreachable` |
| Was connected, master died under a running step | `connection-lost` |
| Was connected, master died, bounded reconnect failed | `unreachable` |

**Teardown is fire-and-forget.** `remote_kill` forks and is not waited for. Its
result is already discarded today, so waiting buys nothing, and it removes a
5 s stall from the grace window — the moment timing matters most. The kill
child must not join the step's process group (that is the group being killed);
`reap_children` already reaps and ignores pids it does not recognise. And if
the master is not alive, skip the remote kill entirely: nothing can be killed
through a dead connection.

**Every remaining call is bounded:**

| Call | Bound | Why |
|---|---|---|
| Pre-connect | 10 s | No timer armed; costs startup time only |
| Mid-run reconnect | 3 s | The one documented slop, on an error path |
| `ssh -O check` | 2 s | Normally 2–3 ms; the bound exists so a wedged master cannot hang the run |
| `ssh -O exit` | 2 s | Same, and it runs after all timers are done |
| Remote kill | none | Fire-and-forget; nothing waits on it |

A mid-run reconnect is the single place where the 100 ms bound does not hold.
That is deliberate and must be written into the spec: after a connection is
lost, other jobs' timers may be late by up to the 3 s reconnect bound. A
safety argument lives on bounded worst cases, and a documented 3 s bound on an
error path is defensible. The alternative — never reconnecting, and failing
every remaining step on that target — turns a one-second network blip into a
dead job and contradicts the `ssh_connection_lost` case, which asserts the next
step succeeds on a fresh master.

## Acceptance criteria

- [x] A step timeout fires within 100 ms of its deadline while another job is starting a master to a healthy target, checked against the microsecond timestamps in `lifecycle.trace`. `e2e: tests/e2e/ssh_preconnect_timeout_is_exact/`
- [x] The same holds while another job's target is **unreachable**: a 1 s step timeout on job A fires within 100 ms even though job B's target is `192.0.2.1`. This is the case that fails today at 10 s. `e2e: tests/e2e/ssh_unreachable_does_not_stall/`
- [x] Every master is opened before the first job's first step spawns, checked from the ssh call log and the trace's ordering. `e2e: tests/e2e/ssh_preconnect_runs_first/`
- [x] Only the targets of the selected jobs are contacted: with `--job` naming a `local` job, a workflow whose inventory holds an unreachable target opens no master and the run succeeds. `e2e: tests/e2e/ssh_preconnect_only_selected/`
- [x] A target that cannot be reached fails its own jobs with reason `unreachable`, skips their dependents with `dependency-failed`, runs every unrelated job, and exits non-zero. `e2e: tests/e2e/ssh_unreachable_fails_its_jobs/`
- [x] `engine-error` no longer appears for an unreachable host, and still appears for a genuine engine fault. `e2e: tests/e2e/ssh_unreachable_fails_its_jobs/`, `tests/e2e/engine_error_spawn/` (existing, must stay green)
- [x] There is no `--no-preconnect` option: passing it is a usage error. `unit: src/cli/run/run_test.c::option_parsing`
- [x] Teardown does not block: a remote step that times out has its grace window honoured within 100 ms, with the remote kill in flight. `e2e: tests/e2e/ssh_teardown_does_not_block/`
- [x] A remote kill is skipped when the master is not alive, and the step still ends. `e2e: tests/e2e/ssh_connection_lost/` (existing, must stay green)
- [x] No parent-side ssh call can hang: with a wedged control socket, `-O check` and `-O exit` give up within their bound rather than hanging the run, and SIGINT is still honoured. `e2e: tests/e2e/ssh_wedged_socket_does_not_hang/`
- [x] A master that dies mid-run is reconnected within the 3 s bound and the next step succeeds. `e2e: tests/e2e/ssh_connection_lost/` (existing, must stay green)
- [x] Every existing ssh case stays green. `e2e: tests/e2e/ssh_hostname/`, `ssh_one_master_sequential/`, `ssh_one_master_parallel/`, `ssh_parallel_rendezvous/`, `ssh_session_cap/`, `ssh_timeout_kills_remote/`, `ssh_master_closed/`, `ssh_env_and_stdin/`, `on_local_override/`
- [x] The 100 ms bound, the 3 s reconnect exception and the `unreachable` reason are written into `docs/qwe-ssh-sec.md` and the design's reason list.

## Comments

> *This was generated by AI during triage.*

Triaged 2026-09-20. Every open question in the original ticket is answered
below; the measurements behind them are in `## What`.

- **The guarantee is exact, at 100 ms.** 50 ms was on the table and is
  achievable — the loop is at <1 ms today — but 100 ms avoids flaky failures on
  a loaded CI machine and still beats today's behaviour by two orders of
  magnitude.
- **Pre-connect over the cheap fix.** Bounding every call only shrinks the
  breach: with `ConnectTimeout=3` a 1 s step timeout can still fire at 3 s.
  Only moving the connect out of the window where timers are armed removes it.
- **Full epoll integration was not chosen.** Pre-connect plus a bounded
  reconnect gets the guarantee to exact everywhere except one documented error
  path, at a fraction of the cost. The "plugins register fds in the event loop"
  primitive stays an open question in qwe-ssh-sec, to be revisited if the 3 s
  reconnect exception proves unacceptable.
- **`--no-preconnect` was proposed and dropped.** Not for code size — it is
  about three lines — but because a flag that silently downgrades a safety
  property is worse to own than the code.
- **Async teardown was chosen on its own merits**, not because the budget
  forced it: `remote_kill`'s result is already discarded.
- **A disabled-host inventory flag is `m1-review/10`**, not this ticket. The
  two must align on scheduling and diverge on outcome; the constraint is
  recorded in that ticket.

## Agent Brief

**Category:** bug

**Summary:** Move every blocking ssh call out of the window where job and step
timers are armed, so a slow or unreachable host cannot delay another job's
timeout. Pre-connect all of the selected jobs' targets before the first job
starts; make teardown fire-and-forget; bound what remains.

**Current behavior:**
`remote_ensure`, `remote_kill` and `close_masters` call `qwe.exec.run`, which
forks and blocks on `waitpid` inside the event loop. A job with a 1.0 s step
timeout was measured being killed at 10 s because a different job was
connecting to an unreachable host. A failed master start reports
`engine-error`.

**Desired behavior:**
A step or job timeout fires within 100 ms of its deadline whatever else the run
is doing, with one documented exception: a mid-run reconnect may cost up to
3 s. An unreachable target fails only its own jobs, with reason `unreachable`.

**Key interfaces:**
- `qwe_run_workflow` gains a pre-connect phase after `select_jobs` and before
  `run_all`, over the distinct non-`local` values of `jobs[i].target`.
- `backend.ssh` keeps `ensure`/`alive`/`kill`/`close_all`; `kill` stops waiting
  for its child, and `ensure` learns a separate bound for a mid-run reconnect.
- `remote_ensure` is not called at spawn time for a target whose pre-connect
  failed; `leader_exit_event` sets `pl->reason = "unreachable"` for that case,
  beside the existing `connection-lost` line.
- The lifecycle table is **not** touched. `unreachable` travels the `R_EVENT`
  payload path that `exit-code` and `connection-lost` already use.

**Acceptance criteria:** the checklist in this ticket's Acceptance criteria
section.

**Out of scope:**
- A disabled-host flag in the inventory (`m1-review/10`).
- Parallel pre-connect. Serial is the decision; revisit only with a lab large
  enough to measure the difference.
- Registering plugin fds in the event loop. Still an open question in
  qwe-ssh-sec, deliberately not solved here.
- Renaming `timeout-minutes` to `timeout-seconds` (`m1-review/02`). It touches this ticket's
  fixtures only incidentally.

Resolved 2026-09-20.

- `qwe.exec.run` gained `timeout` (kills and returns `nil, "timed out after N ms"`) and `background` (returns the pid at once); `qwe.exec.wait(pid, s)` waits for such a child. `backend.ssh` uses them: `-O check` and `-O exit` 2 s, master start `ConnectTimeout` = hard bound (10 s pre-connect, 3 s reconnect), remote kill fire-and-forget and skipped when the master is not up. `close_all` waits (≤5 s each) for kills in flight before `-O exit`, because closing the master would cut them off; that is after every timer is done.
- `workflow.c`: `preconnect()` runs between `select_jobs` and `run_all`, serial, over the distinct targets of selected jobs. A job whose steps are all `on: local` is not counted as using its target: `secret_per_target` and four other fixtures have such jobs on the unreachable `192.0.2.x`, and would otherwise each have cost 10 s.
- A failed connect marks the target `unreachable` in `backend.ssh`; `step_spawn` then skips `remote_ensure`, spawns the step anyway (ssh exits 255 on the missing socket) and `leader_exit_event` gives it reason `unreachable`. A failed mid-run reconnect does the same. A socket-directory *refusal* (ticket 04) is not unreachable: it stays a start-failed at the job, so `ssh_socket_dir_refused` still expects `engine-error`.
- `--no-preconnect` was already a usage error (any unknown option is); `run_test.c::option_parsing` pins that.
- `run_case.sh` keeps the unblanked trace as `lifecycle.raw` so `check.sh` can measure deadlines.
- A stale `$XDG_RUNTIME_DIR` (here it is a root-owned `/run/user/1000`) made every ssh run fail under ticket 04's change. `ensure` now falls back to `/tmp/qwe-<uid>` when it cannot *create* the `qwe` subdirectory; a directory that exists but fails the owner/mode check is still refused.
- Limits of the evidence: `ssh_preconnect_runs_first` proves the master precedes the first step's ssh but cannot tell pre-connect from a lazy connect on a single target (two targets need two distinct host strings, and only one host is reachable here); the exactness cases carry that proof. The wedged-socket case was checked to be non-vacuous by hand: `ssh -O check` on a SIGSTOPped master blocks indefinitely.
- The two unreachable cases each take ~10 s (the bound) and use the TEST-NET address 192.0.2.1; they need a network that drops rather than rejects it.

Follow-up 2026-09-20: one full-suite run failed two tests and could not be reproduced. The likely cause was shared patterns between concurrently running ssh cases (`pkill -f 'ssh -M … xdg/qwe/'` in the wedged case reaches other cases' masters, and two cases waited on `sleep 27182`), so each ssh case that pattern-matches processes now has its own socket-directory name and sleep number. Full runs since were green.
