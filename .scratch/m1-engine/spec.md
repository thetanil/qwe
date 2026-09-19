# M1: the qwe engine

## Goal

Build the qwe engine and show that it works end to end with generic steps on two execution backends, `local` and `ssh`. Every target in M1 is the operator host, reached directly (`local`) or over SSH at `172.18.0.1` (`ssh`, from inside the devcontainer).

M1 has **no** controller, devices, switch, runners, `eth0` changes or deployments. The device-domain language in `CONTEXT.md` (controller, device, pool, protected port, runner) is settled vocabulary, but none of it is built in M1.

Read `CONTEXT.md` first. This spec uses its terms exactly.

## Why engine first

Every later milestone (provisioning `pi00`, runners, flashing) depends on the same hard parts: fork per plugin step, the tee with redaction, check/apply, step outputs, the ssh ControlMaster lifecycle, timeouts and cancellation, and parallelism. M1 builds and tests those against the operator host, where debugging is cheap. That way, bugs found once plugins exist are plugin bugs and not engine bugs. Parallelism is in M1 on purpose, so that it's tested before any domain plugin exists.

## Where this departs from the design docs

`docs/workflow-kernel-design.md` and `docs/qwe-ssh-sec.md` are still the background. Where they conflict with the following decisions, the decisions win:

| Doc says | M1 does |
|---|---|
| A job carries one task (§14). Continue-on-error is per step (§8.2) | Two levels, as in GitHub Actions: a workflow has jobs, and a job has steps |
| Plugins run synchronously on the main thread (§5.3) | Every step plugin runs in a forked child. `uses:` and `run:` steps are both, to the kernel, a process group with pipes |
| The event bus is the heart of the kernel (§4.4) | There is no event bus. The kernel has a service locator. Step plugins talk only through `with:`, step outputs and log bytes |
| Hot reload (§12.4) | Dropped. Every `qwe run` loads plugins fresh |
| Parallelism deferred (§8.3, §17.5) | In M1: workflow `max-parallel`, plus a per-target session cap (default 8) |
| Plugin spine open (§17.1) | Step plugins are the spine. Service plugins provide capabilities in-process |

## User-facing surface

### CLI

```
qwe run <workflow.yaml> [-i <inventory.yaml>] [--job <id>]... [--debug]
qwe validate <workflow.yaml> [-i <inventory.yaml>]
qwe encrypt          # plaintext on stdin, envelope on stdout; never reads argv for the value
qwe keygen           # creates ~/.config/qwe/secret (32 random bytes, mode 0600); refuses to overwrite
qwe serve            # reserved: prints "not implemented", exit 2
qwe --version
```

- **Inventory:** `-i` if given, otherwise `inventory.yaml` in the workflow's directory. If the workflow names any target other than `local` and neither of those exists, it's a validation error.
- `qwe run` always validates first. `qwe validate` is the same validation without running anything, and it never contacts a target.
- `--job <id>` (can be repeated) runs those jobs plus everything they depend on through `needs:`.
- `--debug` makes the lifecycle trace also record every event, including events that changed nothing. Without it the trace still records every transition, every ignored event and any fail-stop abort.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Workflow run finished and every job is `success` or `skipped` |
| 1 | Workflow run finished and at least one job is `failed` or `cancelled` (except by operator cancel) |
| 2 | Usage or validation error. Nothing ran |
| 130 | The operator cancelled the run (SIGINT or SIGTERM to qwe) |

### Workflow YAML (M1 subset)

```yaml
max-parallel: 4                       # optional; default unlimited
env: { GREETING: hello }              # workflow scope
secrets:
  DEMO_TOKEN: !encrypted "v1:…"       # only place envelopes may appear

jobs:
  build:
    target: self                      # a name in the inventory, or `local`
    timeout-minutes: 5
    env: { STAGE: build }             # job scope
    steps:
      - id: greet
        run: echo "$GREETING from $(hostname)"
        env: { EXTRA: x }             # step scope; innermost wins
        timeout-minutes: 1
        continue-on-error: false
      - id: mark
        uses: file.ensure
        with: { path: /tmp/qwe-m1/marker, content: "${{ steps.greet.outputs.who }}", mode: "0644" }
      - on: local                     # the only allowed per-step override
        run: echo "runs on the operator host"
      - run: whoami
        become: true                  # true → root; or a user name; or a uid
  after:
    needs: [build]
    target: local                     # required on every job; there is no default
    steps: [ { run: echo done } ]
```

- **Ids:** a job id and a step `id:` match `[A-Za-z_][A-Za-z0-9_-]*` and are at most 64 characters (GitHub Actions' rule). Anything else is a validation error, so an id is always safe as a file name, a trace field and a template reference.
- **Not in M1:** anchors, aliases and merge keys (rejected, with a source position). Also `if:`, the `always` join rule, `strategy.matrix`, `--check`, `docker`, and expression functions or operators.
- **Templating:** `${{ env.X }}`, `${{ vars.X }}`, `${{ secrets.X }}` and `${{ steps.<id>.outputs.<k> }}` only. They're evaluated in the parent just before a step starts. A reference to anything else is a validation error.
  - `vars` are the job's target's `vars:`. They're **not** exported into the environment automatically. They can be substituted anywhere, including `run:` text, and reach a command's environment only through an explicit `env:` mapping, as in GitHub Actions.
  - `secrets` resolve across the workflow's `secrets:`, the inventory-wide `secrets:` and the job's target's `secrets:`. A name that appears in more than one of those is a validation error.
- **Secrets in `run:` text:** a secret, or anything derived from one, inside `run:` script text is a validation error that points to `env:`. Secrets are allowed in `env:` and `with:` values.

### Inventory (M1 subset)

```yaml
secrets:                                  # inventory-wide
  OTHER: !encrypted "v1:…"
targets:
  self:
    backend: ssh
    host: 172.18.0.1                      # an ssh_config Host name or address, passed to ssh unchanged
    max-sessions: 8                       # optional
    become-password: "${{ secrets.SELF_SUDO }}"   # optional; reserved: M1 requires passwordless sudo and ignores it
    vars: { arch: amd64 }                 # non-secret, host-specific
    secrets:
      SELF_SUDO: !encrypted "v1:…"
```

- **Reference formats** (no new format designed):
  - How to connect is entirely **`ssh_config`**. qwe stores only `host:`, and jumphosts, users, ports and keys live in `~/.ssh/config`.
  - Backends are a discriminated union: `host:` for `ssh`, and later `container:` for `docker`.
  - Device-domain names in M2 follow NetBox's DCIM vocabulary.
- **No inheritance.** No groups, `vars` cascades or defaults files. A shared value is written as an explicit reference by name.
- `local` always exists implicitly and can't be redefined. It has no vars and no secrets.
- Envelopes (`!encrypted`) are allowed only as values inside a `secrets:` map. Other inventory values may reference secrets with `${{ secrets.X }}`, and no other context.

### Step outputs

- **Plugin steps** return outputs to the parent over the child's result pipe, as CBOR. They never travel in the log stream.
- **`run:` steps** write to the file named by `$QWE_OUTPUT` on the target, in GitHub's `$GITHUB_OUTPUT` format: `key=value` lines, plus `key<<DELIM` … `DELIM` for multi-line values. After the command exits, the backend reads the file back and deletes it.
- **Secret outputs** are declared in the plugin's output schema (`secret: true`) or on a `run:` step (`secret-outputs: [key]`). A secret output's plaintext joins the run-wide redaction set as soon as the parent receives it.

### Run artifacts

Each workflow run writes `.qwe/runs/<run-id>/` next to the workflow:
- `<job-id>.log`: the job's merged stdout and stderr, redacted.
- `result.json`: for every job and step, its outcome, reason, `changed`, start and end times, and non-secret step outputs.
- `lifecycle.trace`: the lifecycle trace, one line per record: monotonic time, job, step index, state, event, next state, cell kind (transition, ignore or impossible) and reason. It always has every transition, every ignored or stale event and a fail-stop abort (flushed before the abort). With `--debug` it has every event too. Its name cannot collide with a `<job-id>.log`.

The terminal shows each job's lines prefixed with the job id. E2e golden tests compare against `result.json` and the log files, with times and run id normalized.

## Behaviour (what the tickets must prove)

1. **Jobs and steps.** Steps run in order. A failed step fails its job, unless the step has `continue-on-error`, in which case the job continues and the step's outcome is still `failed`. `needs:` uses the `default` join rule: a job runs only if every dependency is `success`, and is otherwise `skipped` with reason `dependency-failed`.
2. **Outcomes and reasons.** Outcomes are `success | failed | skipped | cancelled`, with no other states. Reasons used in M1: `exit-code`, `timeout`, `cancel-requested`, `dependency-failed`, `not-converged`, `connection-lost`, `become-denied`, `plugin-error`, `engine-error`. `engine-error` means qwe itself could not start the step or job (a log, pipe, timer or fork failed). The step, or the job if it failed at start, is `failed`, `continue-on-error` does not apply, other jobs carry on, and the lifecycle trace records the operation and errno.
3. **Timeouts.** When a step times out, the step is `failed` with reason `timeout`, and `continue-on-error` applies. When a job times out, the job is `cancelled` with reason `timeout`, no further steps start, and nothing can override it. Both are torn down the same way: SIGTERM to the step's process group, a 10-second grace period, then SIGKILL.
4. **Cancel.** SIGINT or SIGTERM to qwe tears down every running job the same way. Those jobs are `cancelled` with reason `cancel-requested`, pending jobs are `skipped`, and qwe exits 130 with no processes left behind.
5. **Fork per step plugin.** Every `uses:` and `run:` step runs in a forked child in its own process group. A plugin that segfaults, blocks forever or leaks memory affects only its own step.
6. **Check/apply.** The kernel calls check, calls apply only if check reports a change is needed, then calls check again. If the second check still reports a change is needed, the step is `failed` with reason `not-converged`. `changed` is true only if apply ran. `run:` steps always report changed.
7. **Parallelism.** Jobs whose dependencies are satisfied run at the same time, up to `max-parallel`. The ssh backend also caps concurrent sessions per target at 8 (overridable in the inventory with `max-sessions:`). Jobs over the cap wait; they don't fail. Each job has its own ring buffer and log. No bytes from one job appear in another job's log.
8. **The ssh backend.**
   - The parent alone starts one ControlMaster per target, in its own session, outside every step's process group. It does this before forking the first step for that target.
   - Steps use `ssh -S <sock> -o ControlMaster=no`. The ControlPath is kept short (`/tmp/qwe-<uid>/%C`) because Unix socket paths have a length limit.
   - Masters are closed with `ssh -O exit` when the run ends.
   - If the master dies partway through a step, the step is `failed` with reason `connection-lost`, and the parent re-establishes the master before the next step. No step is ever retried automatically.
9. **Env and stdin preamble.** All declared env (workflow, then job, then step, innermost wins) reaches the command through a preamble on stdin. The remote `sh` reads the preamble exactly and then passes the rest of stdin to the command unchanged. Env values never appear in argv on either host.
10. **Become.** `sudo -n` wraps the shell that reads the preamble, so env survives `env_reset`. `true` means root, a string means `-u <name>`, and an integer means `-u '#<uid>'`. If sudo refuses, the step is `failed` with reason `become-denied`, immediately and without hanging.
11. **Secrets.**
    - `qwe keygen` creates the key file with mode 0600. qwe refuses a key file that is group- or world-readable.
    - `qwe encrypt` reads the plaintext on stdin and produces the envelope: `version ∥ alg-id ∥ nonce ∥ ciphertext ∥ tag`, using XChaCha20-Poly1305 via libsodium.
    - Values are decrypted in the parent just before the step that needs them. Every secret plaintext is redacted to `***` at the tee, for the rest of the run, before it reaches any log or the terminal.
12. **Plugins.**
    - **Built-in plugins** are compiled into the binary as LuaJIT bytecode. **Project plugins** are loaded from source out of `.qwe/plugins/<name>/` next to the workflow.
    - A project plugin with the same name as a built-in plugin is a validation error.
    - Plugin code runs with strict globals: reading or writing an undeclared global is an error.
    - `qwe validate` checks every plugin's schemas against the JSON Schema metaschema, checks every project plugin's contract (it exports check/apply or is `run`-like, has a `with:` schema, and declares outputs with secret flags), and runs luacheck on project plugins.
    - Built-in plugins go through the same checks as Bazel tests.
13. **Positions.** Every validation error names `file:line:column`. The YAML→CBOR transcoder keeps a side table from each node to its position (needed later for the `qwe serve` LSP).

## Built-in plugins in M1

- **Step plugins:**
  - `run`
  - `file.ensure` (path, content, mode): a generic check/apply plugin that works through the backend and exercises `changed`.
- **Service plugins:**
  - `local` and `ssh` backends
  - recording backend (used in tests only)
  - log sink: terminal plus files per run
  - inventory reader: targets and secrets
  - file key source

## Test strategy

- **Framework:** greatest (single header, vendored in `third_party/`).
- **C unit tests:** one `cc_test` per module, next to the code. Tests that use processes, signals or timerfds go in their own test binary.
- **Plugin tests:** `plugins/builtin/<name>/test.lua` against the recording backend.
- **E2e tests:** `tests/e2e/<case>/` holds a workflow, an optional inventory, and golden `result.json` and log files. They run the real binary.
- **SSH e2e tests** use `172.18.0.1` and **skip, reporting SKIP and exiting 0**, unless both of these are true:
  - `REMOTE_CONTAINERS` is set (we're in this devcontainer),
  - `ssh -o BatchMode=yes -o ConnectTimeout=5 172.18.0.1 true` succeeds.

  `.bazelrc` passes `--test_env=REMOTE_CONTAINERS --test_env=SSH_AUTH_SOCK --test_env=HOME` so the key agent is reachable. These tests are tagged so that Bazel doesn't cache them (`external`). The target host `zeta` has **no** passwordless sudo, which is exactly what the `become-denied` e2e test needs.

## Decisions made while writing this spec (review these)

These weren't discussed in the grilling session. They're the smallest choices that let the tickets have testable acceptance criteria. **All reviewed and settled.** 1–7 were confirmed as written. 8 was changed: `target:` is required.

1. ~~**The inventory has a `targets:` map in M1.**~~ **Confirmed.** A target is the general entry, and a controller (M2) is a target entry that also holds network, switches and devices. The inventory format section above was changed to match (`host:`, per-target `vars:` and `secrets:`, no inheritance).
2. **GitHub key names:** `timeout-minutes`, `continue-on-error`, `max-parallel`.
3. **Cancel grace period:** fixed at 10 seconds in M1.
4. **Run artifacts:** `.qwe/runs/<run-id>/` with `result.json`. Results are JSON, not CBOR, so tests and people can read them with `jq`.
5. **Exit codes** as in the table above.
6. **Ring size:** fixed at 1 MiB per job. A ring drop is logged as a loud alarm line and recorded in `result.json`.
7. **`secret-outputs:`** is how a `run:` step declares its secret outputs.
8. **Every job must declare `target:`,** including `target: local`. There is no default. A forgotten line would otherwise quietly run work meant for a remote controller on the operator's PC. GitHub requires `runs-on:` for the same reason. `local` stays implicit in the inventory, so it never has to be defined there.

## Open questions

- ~~Which CBOR library and which JSON Schema validator to vendor.~~ **Resolved** (ticket 02, ADR-0009):
  - TinyCBOR `v7.0` in C,
  - our own `qwe.cbor` Lua module,
  - lua-schema `1a14a04` (draft-07) with LPeg 1.1.0 and dkjson 2.8, validating in Lua against a qwe strict metaschema.

  `pattern` isn't supported in M1.
- **Positions for errors inside plugin `schema.json` files.** dkjson gives none. Options: report `schema.json` plus the JSON Pointer, transcode schema files through the YAML edge, or write a small position-aware JSON scanner.
- **The secret tag number, and how secrets satisfy `type: string`.** Settle these in ticket 15.
