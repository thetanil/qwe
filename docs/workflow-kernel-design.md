# Workflow Kernel: Architectural Design Document

**Status:** Draft / planning. Revised 2026-09 to match the decisions in `docs/adr/` and the language in `CONTEXT.md`.
**Target:** Single-binary workflow engine with a fully pluggable core
**Platform:** Linux only
**Implementation language:** C99 (no C++ anywhere)
**Embedded plugin language:** LuaJIT

> Terms in **bold** in this document are defined in `CONTEXT.md`. Where this document and an ADR disagree, the ADR wins.

---

## 1. Purpose & Scope

This document specifies the architecture of qwe, a workflow engine in the style of GitHub Actions. The operator writes a **workflow** as a graph of **jobs**, and each job holds an ordered list of **steps**. The operator controls parallelism, and every command's output is captured as logs. `qwe run` runs one **workflow run** to completion and exits. qwe is a CLI, not a service. A later `qwe serve` subcommand (qwerver) will add a web UI, an API and an LSP server for workflow YAML. It is out of scope here, apart from the source layout leaving room for it (§17).

qwe works like Ansible in that it mostly doesn't run commands on the machine it runs on. **Plugins** run on the **operator host**, and the commands they build run on a **target** through an **execution backend** (`local`, `ssh`, later `docker`). A target needs nothing but `sh`. See ADR-0002.

The distinguishing goal is that **almost everything is a plugin**. Only a small, irreducible kernel is fixed. Plugins are written in LuaJIT and can be added as project plugins without rebuilding the binary. Schemas can change without recompiling.

The document records the decisions made, the alternatives that were weighed, and *why* each choice was made, so the reasoning survives independently of the people who were in the room.

This is a design document. It contains no source code.

---

## 2. Design Principles

These principles recur throughout, and they resolve most of the detailed decisions below.

### 2.1 Primitive-and-trusted in the core; rich-and-risky as a peripheral

The design's most repeated architectural move is a layering split. The kernel contains a **dumb, minimal, trusted** version of a capability, and the **rich, evolving, potentially unsafe** version is a plugin loaded afterward:

| Capability | Trusted core (fixed) | Rich peripheral (plugin) |
|---|---|---|
| Configuration | Primitive bootstrap reader (just enough to know what to load) | Inventory reader, and other config sources |
| Log capture | A bounded buffer per job that always captures output | Log sink that persists, rotates, ships elsewhere |
| Workflow authoring | CBOR + JSON Schema internal contract | YAML front-end transcoder |
| Plugin execution | Fork, process group, pipes, teardown | The plugin's Lua code, isolated in the child (ADR-0001) |
| Secrets | A `decrypt(blob, key)` primitive and redaction at the tee | Key source (file today; agent or KMS later) |

The reasoning is the same each time: the core must be able to *function and guarantee its invariants before any plugin exists*, and the fragile or evolving parts must never be able to compromise the kernel. When in doubt, push capability outward into a plugin and keep the kernel small.

### 2.2 Single binary, single-threaded kernel, a process per step

Deployment is a single statically composed binary. The kernel is single-threaded: one event loop owns all kernel state. Concurrency comes from running many child processes under that loop. Every step, whether a `uses:` plugin step or a `run:` step, is a forked child in its own process group (ADR-0001). This removes whole classes of dependency and complexity (see the rejection of `nng` and threading libraries below).

### 2.3 C99 only, no C++

The host is pure C99. This is a hard constraint, and it rules out several otherwise attractive libraries (moodycamel queues, rapidyaml, the C++ tooling around FlatBuffers / Cap'n Proto). Every dependency named in this document is C (or pure Lua), vendorable, and permissively licensed.

### 2.4 Schemas are data, not code

No serialization or validation choice may require code generation, because a core requirement is that **schemas can change without recompiling the host**. This single constraint eliminates an entire category of otherwise excellent formats, and it's the deciding factor in §6.

---

## 3. Technology Choices

### 3.1 Host language & runtime: C99 + LuaJIT

LuaJIT is the embedded plugin language. It's pure C (no C++ needed to embed it), fast, and small. **Built-in plugins** are compiled into the binary as LuaJIT bytecode. **Project plugins** are loaded as source from the project's plugin directory with no build step. Plugin code runs with strict globals (reading or writing an undeclared global is an error).

### 3.2 Serialization: CBOR

Internally the kernel speaks **CBOR** (RFC 8949). See §6 for the full comparison. Summary: the "update schemas without recompiling" requirement eliminates every code-generation format, and CBOR is chosen over MessagePack because it's a formally specified IETF standard with more precise typing, at essentially the same performance. CBOR tags also carry the "encrypted secret" type (qwe-ssh-sec II.5).

### 3.3 Validation: JSON Schema at runtime

Workflows, inventories and plugin `with:` inputs are validated against **JSON Schema** documents that are stored as data files and loaded at runtime. Validation happens once, at the boundary, before anything runs. The same validation is exposed without running anything as `qwe validate`.

Validation runs **in Lua** (ADR-0009): **lua-schema** (draft-07) on qwe's own LuaJIT state, over tables converted from CBOR by our `qwe.cbor` module. Errors come back as JSON Pointers, which the transcoder's position table maps to `file:line:col`. Every schema is first checked against a **qwe strict metaschema** (draft-07 plus root `additionalProperties: false`), so unknown keywords are errors. `pattern` isn't supported yet.

### 3.4 I/O multiplexing: epoll (not io_uring, not eBPF)

The event loop uses `epoll`. At the expected scale (tens to low hundreds of concurrent subprocesses), `epoll` is simpler and effectively faster than `io_uring`, is proven in production (nginx, Redis, systemd), and works on all Linux kernels. `io_uring`'s advantages appear at 1000+ concurrent operations, which is out of scope. `eBPF` is the wrong layer entirely: it's kernel-side instrumentation, not a userspace I/O mechanism.

### 3.5 Workflow authoring: YAML transcoded to CBOR at the edge

Workflows and inventories are written in YAML, but YAML is **never parsed inside the kernel**. A quarantined edge transcoder converts YAML → CBOR, keeping a table of source positions, before the schema validator or the graph builder runs. See §15. YAML anchors, aliases and merge keys are deliberately unsupported.

### 3.6 Rejected technologies (summary)

| Rejected | In favor of | Core reason |
|---|---|---|
| C++ (all of it) | C99 | Hard project constraint |
| MessagePack | CBOR | CBOR is a formal IETF standard with better typing; parity on speed |
| Protobuf / FlatBuffers / Cap'n Proto | CBOR + JSON Schema | All require code generation, violating "schema change without recompile" |
| `nng` | epoll + pipes (in-process) | 200KB plus IPC complexity for a need that doesn't exist in a single-process deployment |
| Ringbuffer message bus (e.g. moodycamel) | Event-loop workflow engine | C++; and the real problem is subprocess orchestration, not message passing |
| An in-kernel event bus | Service locator + step outputs | Nothing needs broadcast once step plugins are forked (ADR-0004) |
| `io_uring` | `epoll` | Overkill below ~1000 concurrent fds; adds complexity; needs kernel ≥5.1 |
| `eBPF` | (n/a) | Wrong layer: kernel-side, not userspace I/O multiplexing |
| DuckDB | (n/a) | Massively overkill for kernel state |
| An SSH library (libssh2/libssh) | Host OpenSSH binary | See qwe-ssh-sec I.3, I.6 |

---

## 4. The Irreducible Kernel

Six responsibilities genuinely cannot be plugins, because they're what plugins depend on in order to exist. Everything else is a plugin.

1. **Plugin loader / registry.** It loads built-in plugins from embedded bytecode and project plugins from the project's plugin directory, and refuses a project plugin with the same name as a built-in one. It resolves dependencies and load order, and owns the lifecycle. There is no hot reload: every `qwe run` loads plugins fresh.
2. **Extension-point mechanism.** The meta-contract: *an extension point is a named interface plus a config schema*. The two kinds of plugin, **step plugins** and **service plugins** (§12), are extension points declared through this mechanism.
3. **Schema composition + validation.** Assembles the overall contract (a discriminated union) from whatever the loaded plugins registered, then validates workflow and inventory against it. In a fully plugin-based system this is **mandatory core**, because it's the bridge between typed config and dynamically registered plugins.
4. **Kernel context / service locator.** Service plugins register capabilities by name (an execution backend, the log sink, the secrets service), and the kernel and other service plugins look them up by name, never by importing them. There is **no general event bus** (ADR-0004).
5. **Minimal config bootstrap.** Just enough primitive config reading to know which plugins to load. Richer config sources, including the inventory reader, are plugins loaded afterward.
6. **Process host / runtime.** Something has to own the event loop, signal handling, forking steps and the teardown path. The kernel is a library with no `main()`. Each CLI subcommand links it (§17). See §5.

---

## 5. Concurrency & Execution Model

The engine is a **single-threaded, event-driven multiplexer**, the same shape as libuv's core, and neither a thread pool nor async/await. One thread owns all kernel state. Concurrency comes from multiplexing many child processes' pipes, not from running kernel code in parallel.

### 5.1 Three event sources

The entire kernel is driven by exactly three kinds of event:

1. **A pipe became readable**, delivered by `epoll`. Drain a step's stdout/stderr into its job's ring, or read a plugin step's result pipe.
2. **A child process exited**, delivered by `SIGCHLD` / `waitpid`. Resolve the step.
3. **A timer fired**, delivered by `timerfd` inside the same `epoll` wait. It's a step timeout, a job timeout, or a cancellation grace period.

If the mapping from these three triggers to job and step state transitions (§7) is correct, the event loop is nearly mechanical. If that mapping is vague, no cleverness elsewhere compensates.

### 5.2 Primitives used

All native Linux, with no external dependency:

- `epoll`: monitor N pipes with one syscall per cycle.
- `timerfd`: timeouts and grace periods as file descriptors inside the same `epoll` wait.
- `fork` / `execve`: run every step (§5.3).
- `sigaction` for `SIGCHLD`, or `waitid`: reap children.
- `pipe` + `fcntl(O_NONBLOCK)`: non-blocking I/O so reads never stall the loop.
- `setpgid` + `kill(-pgid, …)`: process-group signalling for cancellation (§9).

### 5.3 Every step is a forked child

Plugin code never runs on the kernel's thread during a step. For every step, the kernel forks. The child already holds a copy of the loaded LuaJIT state and plugin registry:

- A **`uses:` step** runs the plugin's Lua in the child. The plugin sends its commands to the target through the execution backend (they become grandchildren in the same process group), writes log output to stdout/stderr, and returns its result and **step outputs** to the parent as CBOR over a separate result pipe.
- A **`run:` step** is the built-in `run` step plugin. The child asks the backend to translate the script into a local argv and execs it.

This follows ADR-0001. A Lua step that blocks, whether in a tight loop or on a blocking FFI call, blocks only its own child. The kernel keeps servicing timers, signals and other jobs' pipes. LuaJIT debug hooks were rejected as a safeguard because they aren't reliably checked in JIT-compiled traces and can't interrupt blocking C calls. Inside the child, the plugin API can use plain blocking calls, so there are no coroutines.

### 5.4 Why not threads

Threads would add locking, data races, and a concurrency model the design explicitly doesn't want. Isolation comes from processes, and concurrency from multiplexing many processes under one loop (§8.3).

---

## 6. Serialization & Schema

### 6.1 The deciding constraint

Schemas must be changeable without recompiling the host. This immediately eliminates every format whose schema is compiled into generated code.

### 6.2 Comparison

| Format | Schema location | Code generation | Notes |
|---|---|---|---|
| **CBOR** *(chosen)* | Runtime (JSON Schema, as data) | None | IETF RFC 8949; precise typing; tags; compact; fast |
| MessagePack | Runtime | None | Very close to CBOR; less formally specified, slightly weaker typing |
| Protobuf | `.proto`, compiled | **Required** | Eliminated: schema baked into generated code |
| FlatBuffers | `.fbs`, compiled | **Required** | Eliminated: zero-copy is nice but codegen kills the requirement |
| Cap'n Proto | `.capnp`, compiled | **Required** | Eliminated: best schema evolution, but codegen and C++-centric tooling |
| JSON (raw) | Runtime | None | Used only as the *schema* language and for `result.json`, not as the internal format |

### 6.3 Decision

**CBOR internally, JSON Schema for validation, schemas stored as files.** Validation is performed **once, at the boundary**, when the workflow and inventory are loaded, before anything runs. Validated data then flows through the kernel without being validated again. Step results cross the child→parent result pipe as CBOR.

### 6.4 Schema composition

The kernel assembles the overall contract from the kernel's own workflow structure plus the `with:` and output schemas of every loaded plugin. Because plugins register at load time, the effective schema is only known after loading, which is why schema composition and validation are core (§4.3). `qwe validate` also checks every plugin schema against the JSON Schema metaschema, so a malformed plugin schema (for example `"requried"`) can't silently validate nothing.

---

## 7. Job and Step Lifecycles

The kernel's real job is managing state transitions at two levels: jobs in the graph, and steps inside a running job. Every finished job and step has an **outcome** (`success | failed | skipped | cancelled`) and a **reason** recorded next to it. There are no other terminal states, and "why" is always carried by the reason (for example `timeout`, `cancel-requested`, `dependency-failed`, `exit-code`, `not-converged`, `connection-lost`, `become-denied`, `plugin-error`).

### 7.1 Job states

| State | Meaning |
|---|---|
| `pending` | In the graph, waiting on unmet dependencies |
| `ready` | Dependencies satisfied; waiting for a parallelism slot (§8.3) |
| `running` | Its steps are executing, one at a time |
| `terminating` | Teardown in progress: `SIGTERM` sent to the current step's process group, grace timer armed (§9) |
| `success` / `failed` / `skipped` / `cancelled` | Resolved, with a reason |

### 7.2 Job transitions and their triggers

- `pending → ready`: a dependency resolved and the join rule (§8.2) is satisfied.
- `pending → skipped` (reason `dependency-failed`): a dependency resolved in a way the join rule can't accept.
- `ready → running`: a parallelism slot became available (`max-parallel`, and the target's session cap).
- `ready → skipped` (reason `cancel-requested`): an operator cancel arrived while the job waited for a slot. It never starts.
- `running → success`: the last step finished and no step failed without `continue-on-error`.
- `running → failed`: a step failed without `continue-on-error`. Its remaining steps don't run.
- `running → terminating`: the **job timeout** fired, or an **operator cancel** arrived. Both use the same teardown path.
- `terminating → cancelled`: the current step exited within the grace period, or the grace timer fired and `SIGKILL` was sent. The reason is `timeout` or `cancel-requested`.

### 7.3 Step lifecycle (inside a running job)

A step is `pending → running → success | failed`, or `skipped` if its job ends before it starts, or `cancelled` if its job is torn down while it runs.

- **Step timeout** (the step's own `timeout-minutes`): the step is torn down the same way (§9), and its outcome is **`failed`** with reason `timeout`. `continue-on-error` applies. A step timeout is a limit the author scoped to one step, and it's recoverable.
- **Job timeout or operator cancel**: the current step is torn down, and the step and job are **`cancelled`**. `continue-on-error` can't override it. It's a limit imposed from outside.
- **Plugin steps with check/apply** (§12.3): the step fails with reason `not-converged` if the check after apply still reports a change is needed.

The important observations:
- **Timeout and manual cancellation share one teardown mechanism.** What differs is the outcome, depending on the level the trigger came from.
- **Output draining** (event source 1) goes on throughout `running` without changing any state.

---

## 8. Failure Semantics

### 8.1 Four outcomes, not two

GitHub Actions conflates concepts that this design keeps apart. `skipped` in particular is *not* a failure. It means the job or step never ran, because upstream outcomes didn't satisfy its join rule or its job ended first.

### 8.2 Continue-on-error and join rules

**`continue-on-error` is per step.** A failed step marked `continue-on-error: true` keeps its `failed` outcome, but the job carries on with its next step and doesn't fail because of it. Without it, the step's failure fails the job.

**Join rules are per job**, deciding whether a job runs given its dependencies' outcomes:

- **default:** run only if every dependency is `success`. Otherwise `skipped` with reason `dependency-failed`.
- **always:** run regardless of dependency outcomes. Deferred past M1.

Further conditions in the GitHub Actions family (`if:`, `failure()`, expressions) are deferred.

### 8.3 Parallelism

Parallelism is controlled by the user in the GitHub Actions style, and it's built in M1, before any domain plugin, so its correctness is established while the only plugins are generic ones. Ready jobs are started as long as:

- fewer than **`max-parallel`** jobs are running (a workflow-level setting; unlimited by default), and
- the job's target has a free session under its **session cap**. The ssh backend caps concurrent sessions per target at 8 by default (inventory `max-sessions:`), because OpenSSH's server `MaxSessions` defaults to 10 per multiplexed connection. Jobs above the cap wait. They don't fail.

The graph and state machines are unchanged: parallelism is only a gate on `ready → running`. Each running job has its own ring and log. Matrix-style fan-out is deferred.

---

## 9. Cancellation & Timeouts

### 9.1 Teardown is an asynchronous state, not an instant

```
step running → (step timeout | job timeout | operator cancel) → teardown
    ├── process group exits within grace  → done
    └── grace timer fires → SIGKILL        → done
```

On entering teardown, the kernel sends `SIGTERM` to the step's process group and arms a **grace timer** (a `timerfd`, 10 seconds in M1). If the group exits before the timer fires, teardown is clean. If the timer fires first, the kernel escalates to `SIGKILL`. The outcome depends on the trigger (§7.3).

Operator cancel is SIGINT or SIGTERM to qwe. Every running job is torn down and becomes `cancelled` with reason `cancel-requested`, pending jobs become `skipped`, and qwe exits 130.

### 9.2 Distinct classes of timer

The **step timeout**, the **job timeout** and the **cancellation grace period** are different timers with different meanings. A timeout firing is a *trigger into* teardown. The grace timer operates *inside* teardown. They must not be conflated, and firing one never re-arms another.

### 9.3 Process groups, not PIDs

Subprocesses may themselves spawn children (a shell running a pipeline, or a plugin running commands through the backend). Signalling only the direct PID leaves orphans. So each step's child calls `setpgid` to become a process-group leader, and the kernel signals the **whole group** via `kill(-pgid, …)`. For ssh targets, killing the local `ssh -S` client closes the channel. The SSH ControlMaster is deliberately **outside** every step's process group, so cancelling a step never kills the shared connection (qwe-ssh-sec I.2).

---

## 10. Output Handling & Backpressure

### 10.1 Requirements recap

A step's stdout and stderr have two consumers: **the system always logs them**, and live consumers (the terminal display today, `qwe serve` later) may follow them. The log sink is itself a service plugin. Output must be captured reliably regardless of plugin state, buffering is bounded with explicit drop accounting, and **secrets are redacted before any consumer sees a byte**.

Values a step produces are *not* output. **Step outputs** travel separately over the result pipe or the `$QWE_OUTPUT` file (ADR-0005), never in this stream.

### 10.2 The bootstrap problem

Because the log sink is a plugin, output would have nowhere to go until that plugin loads, and early output is often the most interesting. Resolution (per §2.1): **the kernel owns a bounded in-memory ring that always exists**, and the log sink is a *consumer that drains it*, not the buffer itself.

### 10.3 Per-job ring, single privileged reader

- There is **one bounded ring per job** (1 MiB in M1). A job's steps run one after another, so a job's ring holds its steps' output in order.
- The ring is sized and governed against **exactly one privileged reader: the log sink**. The ring's fill state is defined only by the log-sink cursor.
- A drop **at the ring** means the log sink itself fell behind, so the source of truth lost bytes. This is a **loud alarm**, written to the log and recorded in `result.json`.

### 10.4 Redaction at the tee

As bytes are drained from the ring toward the log sink and live consumers, every **secret** plaintext currently known to the workflow run is replaced with `***`. The known set grows during the run: decrypted `secrets:` values join it when they're materialized for a step, and secret step outputs join it when the parent receives them. A secret is masked for the rest of the run, not only in the step that used it. Matching must work across chunk boundaries. See qwe-ssh-sec II.7.

### 10.5 Live consumers are downstream and isolated

Live consumers don't get their own competing cursor into the ring. As chunks are drained toward the log sink, they're fanned out to subscribed live consumers, each with its own small buffer. If a consumer can't keep up, the drop happens **in that consumer's feed**, and its stream is marked "lost K bytes." A slow consumer therefore never stalls the ring, the pipe, the running job, or any other consumer.

### 10.6 The full backpressure chain

```
step child (and its grandchildren, e.g. ssh -S …)
  └─ OS pipe buffer             ← backpressure applied to the step itself
      └─ epoll-driven read into the per-job ring (bounded, drop-accounted)
          └─ redaction at the tee
              ├─ log sink cursor        ← privileged; ring sized so this never drops
              └─ live consumer feeds    ← droppable; each isolated, warned on loss
```

The key property: **a step can't flood the engine.** A fast producer fills the OS pipe buffer (default ~64KB) and its own `write()` blocks. That's the right kind of backpressure on the step, and it costs the single-threaded loop nothing, because the engine drains the pipe at its own pace. The ring drops only because a *consumer* is slow, never because a *producer* is fast.

---

## 11. Stream Interleaving

### 11.1 Decision: merged, best-effort interleave

`stdout` and `stderr` are **merged into a single stream in arrival order**. Whichever fd is readable is read, and what it holds is appended immediately. Stream origin isn't preserved, which matches GitHub Actions.

### 11.2 Known limitation, accepted

This is a *best-effort* interleave, not a byte-exact global order. If both pipes are readable in the same `epoll` wakeup, the engine picks one to read first. For human-readable logs this can't be told apart from true ordering. Byte-exact ordering would need both fds `dup2`'d onto one pipe in the child, which remains an option because origin is already being dropped.

---

## 12. Plugin Model

### 12.1 Two kinds of plugin

| | **Step plugin** | **Service plugin** |
|---|---|---|
| Examples | `run`, `file.ensure`, later `power.ensure`, `runner.ensure` | `local` / `ssh` backends, recording backend, log sink, inventory reader, key source |
| Runs | In a forked child, once per step (§5.3) | In-process, for the whole workflow run |
| Interfaces | `with:` inputs in; step outputs, log bytes and a result out | Registered by name in the service locator |

**Step plugins are the spine.** A GitHub-Actions-style engine is mostly step plugins: take inputs, do work on a target, report a result. This resolves the former open question about event-subscriber plugins: there are none, and there is no event bus (ADR-0004).

### 12.2 Where plugins come from

- **Built-in plugins** are compiled into the binary as LuaJIT bytecode, from `plugins/builtin/<name>/{plugin.lua,schema.json}`.
- **Project plugins** are loaded from source out of `.qwe/plugins/<name>/` next to the workflow, using the same layout.
- A project plugin with the same name as a built-in plugin is a **validation error**. Otherwise a stray project `power.ensure` could replace the built-in one and drop its safety checks.

`qwe validate` runs three checks on every project plugin: its schemas against the metaschema, its **plugin contract** (it exports check/apply or is `run`-like, has a `with:` schema, and declares outputs with `secret` flags), and **luacheck**. Built-in plugins pass the same checks as Bazel tests. All plugin code runs with strict globals.

### 12.3 Check and apply

qwe is idempotent in the Ansible sense: running a workflow a second time should change nothing. A step plugin provides **check** (is the target already in the desired state? It never changes anything) and **apply** (move it there). The kernel runs check, runs apply only if check reports a change is needed, then runs check again. The step's **changed** flag records whether apply ran. If the second check still reports a change is needed, the step fails with reason `not-converged`. `run:` steps can't be checked, so they always report changed. A future `--check` mode is "run only check."

### 12.4 Inputs and outputs

- **Inputs:** a step's `with:` block, validated against the plugin's schema when the workflow is loaded. Values may use `${{ env.* }}`, `${{ secrets.* }}` and `${{ steps.<id>.outputs.* }}`, which are evaluated in the parent just before the step starts.
- **Outputs:** named values returned by the step, readable by later steps **in the same job only**. An output declared `secret` joins the redaction set as soon as the parent receives it.

### 12.5 Lifecycle

Plugins are loaded, their schemas registered, and their service plugins started once at the start of each `qwe run`. Service plugins are stopped at the end of the run (for example, the ssh backend closes its ControlMasters). There is no hot reload: a CLI that runs to completion loads everything fresh each time.

---

## 13. Configuration Bootstrap and Inventory

The kernel contains only a **primitive bootstrap reader**, enough to decide which plugins to load. Everything richer is a service plugin loaded afterward.

The **inventory** describes the world a workflow acts on. It's a separate file from the workflow, so one workflow can run against different inventories: `-i <file>`, or by default `inventory.yaml` in the workflow's directory. In M1 it holds an inventory-wide `secrets:` map and a `targets:` map. Each target has:
- typed backend fields: `backend: ssh` with `host:`, plus an optional `max-sessions`,
- its own `vars:` (non-secret, host-specific),
- its own `secrets:`.

A job reads its target's values as `${{ vars.X }}` and `${{ secrets.X }}`, the way GitHub Actions reads a deployment environment's values. Controllers with their network, switches and devices come in later milestones, as target entries with more fields. The `local` target always exists implicitly. The inventory reader is a service plugin, and the kernel only resolves each job's `target:` to a backend.

**The format reuses existing standards rather than inventing one.**
- How to *reach* a host is entirely **`ssh_config`**: qwe stores only `host:`, and jumphosts, users, ports and keys live in `~/.ssh/config`.
- The M2 device domain takes its names from **NetBox**'s DCIM model.
- Unlike Ansible and Nornir, **nothing is inherited**: there are no groups, var cascades or defaults, and every value is written where it applies. Ansible's many levels of variable precedence are the thing being avoided.

---

## 14. Workflow Model

- A **workflow** is a graph of **jobs** connected by `needs:`. Cycles are validation errors.
- A **job** has an id, dependencies, exactly **one target**, which must be declared (`target: local` included; there is no default), a timeout, a join rule, an `env:` block, and an ordered list of **steps**.
- A **step** is a `uses:` plugin step or a `run:` step (the built-in `run` plugin). It has an optional id, `with:` inputs, `env:`, a timeout, `continue-on-error`, and `become:` (run as root when `true`, as a named user, or as a uid).
- A step may set `on: local` to run on the operator host instead of the job's target. **No other per-step target override exists** (ADR-0003).
- **Env scopes** follow GitHub Actions: workflow, then job, then step, and the innermost wins. Declared env reaches commands through a stdin preamble, never argv (qwe-ssh-sec I.7). A change a step makes to its own environment at run time doesn't carry over.
- **Templating** uses GitHub's `${{ … }}` syntax, with the contexts `env`, `vars`, `secrets` and `steps` in M1. `vars` are never exported into the environment automatically. Secrets are forbidden inside `run:` text.
- The workflow is written in YAML, transcoded to CBOR with positions (§15), validated against the composed schema (§6.4), and built into an in-memory graph. Execution walks the graph through the state machines (§7), gated by parallelism (§8.3), driven by the three event sources (§5.1).
- `qwe run --job <id>` runs the selected jobs plus everything they depend on through `needs:`. The whole workflow is still validated.
- Each workflow run writes `.qwe/runs/<run-id>/` next to the workflow: one redacted log per job and a `result.json` with every outcome, reason and changed flag.

---

## 15. YAML Front-End / Transcoder

### 15.1 YAML is a surface concern only

The kernel speaks CBOR + JSON Schema. YAML exists only for ease of writing. It's transcoded to CBOR **before** it reaches the validator or graph builder, and the kernel never links a YAML parser.

### 15.2 Parser choice: libyaml, quarantined

`libyaml` is the only credible pure-C YAML parser (MIT-licensed, vendorable). Its drawbacks are real, and they're why it's kept at the edge rather than trusted in the core: YAML 1.1 only, effectively in maintenance mode, and a history of CVEs relevant to files supplied by users. Every modern *fast* alternative (e.g. rapidyaml) is C++, which disqualifies it.

### 15.3 The transcoder as an untrusted edge component

1. `libyaml` parses the YAML file into events.
2. The transcoder emits CBOR from those events, **enforcing its own limits** as it goes: maximum nesting depth and maximum document size.
3. It records a **source position** (`line:column`) for every node in a side table (ADR-0008). Every validation error reports `file:line:col`. The future `qwe serve` LSP needs this.
4. The `!encrypted` tag becomes the secret CBOR tag, and the envelope passes through **unchanged and undecrypted**. Any other tag is rejected.
5. Schema validation and graph construction operate **only** on the resulting CBOR, never on YAML.

### 15.4 YAML anchors, aliases and merge keys are rejected

Anchors, aliases and merge keys (`&`, `*`, `<<`) are **rejected with their position**, as in GitHub Actions. Banning them removes a whole class of parser risk and ambiguity.

---

## 16. Dependencies

The complete external dependency set is C or pure Lua, vendorable, and permissively licensed:

| Dependency | Role | Notes |
|---|---|---|
| **LuaJIT** | Plugin runtime | Pure C |
| **TinyCBOR** `v7.0` | Internal serialization (C) | MIT; no allocation; our `qwe.cbor` Lua module sits on top |
| **lua-schema** `1a14a04` + **LPeg** 1.1.0 + **dkjson** 2.8 | JSON Schema draft-07 validation, schema-file parsing | All MIT; pure Lua except LPeg (small C). See ADR-0009 |
| **libyaml** | YAML→CBOR transcoding | Quarantined at the edge (§15) |
| **libsodium** | Secrets | qwe-ssh-sec II.2 |
| **luacheck** + **argparse** (+ **luafilesystem** or API-only use) | Linting project and built-in plugins | Pure Lua (lfs is small C) |
| **greatest** | C unit tests | Single header, ISC; test-only |
| *(none)* | Event loop, timers, subprocess, signals | Native Linux syscalls: `epoll`, `timerfd`, `fork`/`execve`, `sigaction`/`waitid`, `pipe`, `fcntl`, `setpgid`/`kill` |
| Host **OpenSSH** client | ssh backend | Not linked; invoked as a subprocess (qwe-ssh-sec I.3) |

Explicitly **not** dependencies: any C++ library, `nng`, `io_uring` bindings, a threading library, DuckDB, an SSH library, or a TLS stack.

---

## 17. Source Layout, Testing, Open Questions

### 17.1 Source layout

```
src/kernel/        library, no main(); one public header
src/edge/yaml/     YAML → CBOR transcoder
src/secrets/       envelope + decrypt primitive
src/cli/main.c     subcommand table only
src/cli/{run,validate,encrypt,keygen,serve}/
plugins/builtin/<name>/{plugin.lua,schema.json,test.lua}
third_party/       vendored dependencies
tests/e2e/<case>/  real-binary golden tests
```

Each subcommand is its own Bazel package linking the kernel, so `qwe serve` (qwerver: web UI, API and workflow-YAML LSP) grows inside `src/cli/serve/` without reorganizing anything else. It can drive workflow runs by calling the kernel directly.

### 17.2 Testing

- **C unit tests** use greatest, with one `cc_test` per module next to its code. Tests involving processes, signals or timerfds go in their own binary, and Bazel's per-binary process provides the isolation.
- **Plugin tests** (`plugins/builtin/<name>/test.lua`) run against the **recording backend**, which records every command and returns scripted results.
- **End-to-end tests** run the real binary against golden `result.json` and log files. SSH end-to-end tests use a real host and are skipped outside the development container.

Every issue's acceptance criteria name the test that proves them (`docs/agents/issue-tracker.md`).

### 17.3 Open questions / deferred decisions

1. **Positions for errors inside plugin `schema.json` files.** dkjson gives no positions. (The library choice itself is resolved: ADR-0009.)
2. **Live-consumer delivery guarantee.** Best-effort for live consumers, guaranteed for logs. The "lost K bytes" marker format and per-consumer buffer sizing are unspecified.
3. **Ring sizing policy.** Fixed at 1 MiB per job in M1. Whether it should become configurable or adaptive is open.
4. **Join-rule vocabulary.** `always`, `if:` and expression evaluation are deferred.
5. **Matrix / fan-out.** Deferred until a use case requires it.
6. **`--check` mode.** Enabled by the check/apply split, but deferred.
7. **Multi-process / IPC.** Out of scope. `qwe serve` calls the kernel in-process.
8. **io_uring re-evaluation trigger.** Reconsider only if profiling shows `epoll` is the bottleneck *and* concurrency approaches ~1000 fds.

---

## Appendix A: Decision Log (one-line summaries)

- **Host:** C99, Linux only, single binary. *No C++, no threads.*
- **Shape:** CLI that runs to completion. `qwe serve` is a future subcommand, not a daemon mode.
- **Plugins:** LuaJIT. Built-in plugins are embedded bytecode, project plugins are source, and shadowing is refused. Strict globals and luacheck. *No hot reload.*
- **Plugin kinds:** step plugins (the spine, forked per step) and service plugins (in-process). *No event bus* (ADR-0004).
- **Where code runs:** plugins only on the operator host. Commands on targets through execution backends, and targets need only `sh` (ADR-0002).
- **Kernel = 6 fixed responsibilities.** Everything else is a plugin.
- **Serialization:** CBOR internally. *Codegen formats rejected.*
- **Validation:** JSON Schema, schemas as data files, validated once at load. `qwe validate` exposes it.
- **Execution:** single-threaded epoll loop; three event sources; **every step is a forked child in its own process group** (ADR-0001).
- **Workflow:** GitHub Actions style: jobs in a graph, ordered steps within a job, **one target per job** and `on: local` as the only override (ADR-0003).
- **Outcomes:** `success | failed | skipped | cancelled`, always with a reason. Step timeout → `failed`, job timeout or cancel → `cancelled`.
- **Idempotency:** check → apply → check. `changed` flag, `not-converged` failure.
- **Parallelism:** in M1. `max-parallel` plus a per-target session cap of 8.
- **Cancellation:** SIGTERM → grace (10s) → SIGKILL on process groups. One teardown path for every trigger.
- **Output:** one ring per job, redaction at the tee, log sink as the privileged reader, and isolated live consumers. **Step outputs travel on a separate channel** (ADR-0005).
- **Streams:** stdout+stderr merged, best-effort interleave, origin dropped.
- **Authoring:** YAML → CBOR at a quarantined libyaml transcoder, **with source positions** (ADR-0008); anchors, aliases and merge keys rejected.
