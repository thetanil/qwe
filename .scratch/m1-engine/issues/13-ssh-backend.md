# 13: The ssh backend

Status: ready-for-agent
Type: task
Blocked by: 08, 11, 12

## What

Build the `ssh` execution backend as described in the spec:
- **Master lifecycle, in the parent only.** The parent starts one ControlMaster per target, in its own session, before forking the target's first step. It uses a short ControlPath (`/tmp/qwe-<uid>/%C`) and closes the master with `ssh -O exit` at the end of the run.
- **Translation.** Commands become `ssh -S <sock> -o ControlMaster=no <dest> -- sh -c '<preamble reader>; exec <cmd>'`.
- **Connection loss.** If the master dies partway through a step, the step fails with reason `connection-lost`, the master is re-established before the next step, and nothing is retried.
- **Session cap.** A per-target limit (default 8, `max-sessions:` in the inventory). Jobs over the cap wait instead of failing.
- **`on: local`.** The step runs on the operator host and still sees the job's outputs.

The e2e tests use `target: self → 172.18.0.1` and **skip** (report SKIP, exit 0) unless `REMOTE_CONTAINERS` is set and `ssh -o BatchMode=yes -o ConnectTimeout=5 172.18.0.1 true` succeeds.

## Acceptance criteria

- [ ] `run: hostname` on `self` prints the remote host's hostname. `e2e: tests/e2e/ssh_hostname/`
- [ ] Ten sequential steps on `self` use exactly one master: count the `ssh -M` processes started during the run. `e2e: tests/e2e/ssh_one_master_sequential/`
- [ ] Two jobs on `self` starting at the same moment result in exactly one master. `e2e: tests/e2e/ssh_one_master_parallel/`
- [ ] The rendezvous test from ticket 08 passes with both jobs on `self`. `e2e: tests/e2e/ssh_parallel_rendezvous/`
- [ ] With `max-sessions: 1`, two jobs on `self` run one after the other, and neither fails. `e2e: tests/e2e/ssh_session_cap/`
- [ ] A step timeout on `self` leaves no remote `sleep` running (checked with `pgrep` over ssh afterwards). `e2e: tests/e2e/ssh_timeout_kills_remote/`
- [ ] Killing the master partway through a step makes that step `failed` with reason `connection-lost`, and the next step succeeds on a new master. `e2e: tests/e2e/ssh_connection_lost/`
- [ ] No master survives the end of the run, including after SIGINT. `e2e: tests/e2e/ssh_master_closed/`
- [ ] Cancelling a step doesn't kill the master: the master's pgid isn't any step's pgid. `unit: src/kernel/proc_test.c::master_outside_step_groups`
- [ ] Env and stdin through the preamble work remotely exactly as they do locally. `e2e: tests/e2e/ssh_env_and_stdin/`
- [ ] An `on: local` step in a `self` job runs on the operator host, and a later `self` step reads its output. `e2e: tests/e2e/on_local_override/`
- [ ] Translation for a fixed input produces the exact expected argv. `unit: plugins/builtin/backend-ssh/test.lua::translation_golden`
- [ ] Outside the devcontainer, every ssh e2e case reports SKIP and passes. `manual: run bazel test //tests/e2e/... with REMOTE_CONTAINERS unset and confirm the SKIP lines`
