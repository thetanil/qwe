# 13: The ssh backend

Status: resolved
Type: task
Blocked by: 08, 11, 12, 18

## What

Build the `ssh` execution backend as described in the spec:
- **Master lifecycle, in the parent only.** The parent starts one ControlMaster per target, in its own session, before forking the target's first step. It uses a short ControlPath (`/tmp/qwe-<uid>/%C`) and closes the master with `ssh -O exit` at the end of the run.
- **Translation.** Commands become `ssh -S <sock> -o ControlMaster=no <dest> -- sh -c '<preamble reader>; exec <cmd>'`.
- **Connection loss.** If the master dies partway through a step, the step fails with reason `connection-lost`, the master is re-established before the next step, and nothing is retried.
- **Session cap.** A per-target limit (default 8, `max-sessions:` in the inventory). Jobs over the cap wait instead of failing.
- **`on: local`.** The step runs on the operator host and still sees the job's outputs.

The e2e tests use `target: self → 172.18.0.1` and **skip** (report SKIP, exit 0) unless `REMOTE_CONTAINERS` is set and `ssh -o BatchMode=yes -o ConnectTimeout=5 172.18.0.1 true` succeeds.

## Acceptance criteria

- [x] `run: hostname` on `self` prints the remote host's hostname. `e2e: tests/e2e/ssh_hostname/`
- [x] Ten sequential steps on `self` use exactly one master: count the `ssh -M` processes started during the run. `e2e: tests/e2e/ssh_one_master_sequential/`
- [x] Two jobs on `self` starting at the same moment result in exactly one master. `e2e: tests/e2e/ssh_one_master_parallel/`
- [x] The rendezvous test from ticket 08 passes with both jobs on `self`. `e2e: tests/e2e/ssh_parallel_rendezvous/`
- [x] With `max-sessions: 1`, two jobs on `self` run one after the other, and neither fails. `e2e: tests/e2e/ssh_session_cap/`
- [x] A step timeout on `self` leaves no remote `sleep` running (checked with `pgrep` over ssh afterwards). `e2e: tests/e2e/ssh_timeout_kills_remote/`
- [x] Killing the master partway through a step makes that step `failed` with reason `connection-lost`, and the next step succeeds on a new master. `e2e: tests/e2e/ssh_connection_lost/`
- [x] No master survives the end of the run, including after SIGINT. `e2e: tests/e2e/ssh_master_closed/`
- [x] Cancelling a step doesn't kill the master: the master's pgid isn't any step's pgid. `unit: src/kernel/proc_test.c::master_outside_step_groups`
- [x] Env and stdin through the preamble work remotely exactly as they do locally. `e2e: tests/e2e/ssh_env_and_stdin/`
- [x] An `on: local` step in a `self` job runs on the operator host, and a later `self` step reads its output. `e2e: tests/e2e/on_local_override/`
- [x] Translation for a fixed input produces the exact expected argv. `unit: plugins/builtin/backend-ssh/test.lua::translation_golden`
- [x] Outside the devcontainer, every ssh e2e case reports SKIP and passes. `manual: run bazel test //tests/e2e/... with REMOTE_CONTAINERS unset and confirm the SKIP lines`

## Comments

- **Where it lives.** `plugins/builtin/backend-ssh/ssh.lua` (module `backend.ssh`): translation (`command_argv`, run in the step's child) and the parent-only lifecycle (`ensure`, `alive`, `kill`, `close_all`), all through `qwe.exec.run`. The kernel calls it from `workflow.c` (`remote_ensure`, `remote_kill`, `remote_lost`, `close_masters`). `qwe.exec` gained `getpid`, `getuid` and a `detach` option (own session, no stdout/stderr) for the master.
- **Decisions the ticket left open.**
  - **ControlPath is per run:** `/tmp/qwe-<uid>/<qwe pid>.%C`. With one path per user and host, two `qwe run`s (parallel tests) would share one master, and the first to finish would close it under the other.
  - **A session is a running job.** A job on a non-local target holds one of its target's sessions from start to end (`qwe_sched_pass` groups, cap from `max-sessions:`, default 8). Jobs over the cap stay `ready`.
  - **No new connection from a step.** Clients pass `-o ProxyCommand=false`. Without it, a client whose master died mid-session falls back to a fresh direct connection (`ControlMaster=no` allows it) and the step keeps running unowned instead of failing with `connection-lost`. Found by the `ssh_connection_lost` case.
  - **Connection lost** = the client exited 255 and `ssh -O check` finds no master. It applies to `run:` steps; a plugin step that loses its connection fails as `plugin-error`.
  - **Killing a step also kills its remote processes.** sshd gives a command without a tty no signal when its channel closes, so killing the local client left `sleep` running on the host. Every remote step gets `QWE_STEP=<run id>.<job>.<step>` in its environment, and on TERM/KILL the parent runs a small `sh` over the master that signals every `/proc/*/environ` carrying it (needs `/proc`, `tr`, `grep` on the target). It is skipped once the local client has exited.
  - **A failed master start** fails the step with reason `engine-error` (the start-failed path), with ssh's message on stderr.
- **Not done, on purpose.** `$QWE_OUTPUT` is not set for a remote `run:` step (the file would be on the wrong host), so such a step cannot produce outputs yet. `on: local` steps can, and a later remote step reads them (`on_local_override`). The master start blocks the event loop for up to its 10 s connect timeout.
- **Tests.** All `ssh_*` cases carry `needs-ssh`; `run_case.sh` skips them (prints SKIP, passes) unless `REMOTE_CONTAINERS` is set and `ssh 172.18.0.1 true` works. Cases that count masters use a `bin/ssh` shim (`run_case.sh` puts `bin/` first on PATH). Unit tests: `sched_test` (session cap), `proc_test::master_outside_step_groups`, `backend-ssh/test.lua`.
- Ran the SKIP check with `env -u REMOTE_CONTAINERS`: every ssh case prints SKIP and passes.
