# qwe

qwe (Quantum Workflow Engine) is a single-binary Linux command-line workflow engine. It runs a GitHub-Actions-style workflow to completion and then exits. Almost all of its capabilities come from LuaJIT plugins. The first plugins manage a small fleet of physical devices: power, flashing, VLAN placement and probing.

## Language

**Workflow**:
An authored definition of work, laid out as a directed acyclic graph of jobs.
_Avoid_: Pipeline, playbook

**Workflow run**:
One execution of a workflow by `qwe run`, from start until it exits. A device operation such as a flash is a workflow run. It is not a special kind of job.
_Avoid_: Execution, device job, operation

**Job**:
A node in a workflow's graph. It is the unit that dependencies, join rules and parallelism apply to. It has exactly one target and holds an ordered list of steps. Any step may override the target only to run on the operator host.
_Avoid_: Task, node, stage

**Step**:
One entry in a job's ordered list. It either invokes a plugin (`uses:`) or runs a shell command (`run:`). A job's steps run one after another. Every step inherits the declared `env:` of its workflow and job, and its own `env:` overrides those. A step passes values to later steps only through step outputs. A step changing its environment at run time does not carry over to the next step. `run:` is itself a built-in step plugin.
_Avoid_: Action, task, command

**Step output**:
A labelled value a step produces. Later steps in the same job can read it. Steps in other jobs cannot.
_Avoid_: Result, return value, artifact, job output

**Outcome**:
How a finished step or job resolved. It is exactly one of `success`, `failed`, `skipped` or `cancelled`. `skipped` means it never ran, and it is not a failure.
_Avoid_: Status, conclusion, result

**Reason**:
Why an outcome happened, recorded next to it. Examples are `timeout`, `cancel-requested`, `dependency-failed`, `not-converged` and `engine-error` (qwe itself failed, not the step). A timed-out step is `failed` with reason `timeout`. A timed-out job is `cancelled` with reason `timeout`.
_Avoid_: Cause, error code

**Job lifecycle**:
The states one job passes through, from when the workflow is parsed until it has an outcome. It includes waiting on needs and for a parallelism slot, and the phase of the step running now. Every change to a job's state is a transition of its lifecycle.
_Avoid_: Job status, run state

**Trigger**:
Something that ends a running step early: the step's timeout, the job's timeout, or an operator cancel. The trigger decides the outcome and reason. When several fire, the strongest wins: cancel, then job timeout, then step timeout.
_Avoid_: Interrupt, abort, kill

**Lifecycle trace**:
The record a workflow run keeps of its job lifecycles: every transition, every event it ignored, and why. It is the evidence of what the engine decided, separate from what the steps printed.
_Avoid_: Debug log, audit log, event log

**Teardown**:
Stopping a running step: it is asked to stop, given a grace period, and then forced to stop. Every trigger uses the same teardown.
_Avoid_: Kill, abort, shutdown

**Check**:
The part of a step plugin that reports whether the target is already in the desired state. It never changes anything.
_Avoid_: Probe (a probe is a device-status plugin), test, verify

**Apply**:
The part of a step plugin that changes a target toward the desired state. It runs only when check reports a change is needed, and check runs again afterwards to confirm. If that second check still reports a change is needed, the step fails with reason `not-converged`.
_Avoid_: Run, execute, fix

**Changed**:
A yes/no flag on a successful step saying whether apply ran. `run:` steps always count as changed.
_Avoid_: Dirty, modified

### Where work happens

**Operator host**:
The machine where qwe runs, usually the operator's own PC. All plugin code runs here, and nowhere else.
_Avoid_: Control node, controller, localhost

**Target**:
A host where a step's commands run. It can be the operator host itself, a remote machine reached over SSH, or a container. A target needs no qwe and no Lua. In the inventory, a target has its own `vars` (non-secret values specific to that host) and `secrets`, and a job reads them through its target. Nothing is inherited from groups or defaults.
_Avoid_: Remote, node, runner, agent, environment (GitHub's closest equivalent, but "environment" here means env variables)

**Inventory**:
The operator's description of the world a workflow acts on: its targets, and later its controllers, devices, pools and secrets. It is kept separate from workflows, so one workflow can run against different inventories.
_Avoid_: Hosts file, lab file, config

**Controller**:
A target that is physically wired to devices and is reached only remotely, for example a Raspberry Pi in a datacenter behind an SSH jumphost. It is a role a target plays, not a component of qwe.
_Avoid_: Controller node (as used in the device-manager spec, where it meant the machine running the whole service)

**Device**:
A managed piece of hardware attached to a controller, typically automotive hardware, often running QNX on a read-only filesystem. A device is never a target: qwe never runs commands on it. It is only acted on by commands the controller runs (switch PoE, netboot, fastboot, serial). Its power, image and network placement are what the first plugins manage. Also called a DUT (device under test).
_Avoid_: Board, node, host, slot

**Pool**:
A named set of devices on one controller, assigned to exactly one runner so that CI jobs on different runners never use the same device. A pool usually holds one or two devices, but the size is not constrained. The boundary is a convention only: nothing stops a runner from touching another pool's devices.
_Avoid_: Group (it clashes with GitHub runner groups, Ansible groups and Unix groups), partition, slot

**Protected port**:
A switch port that qwe must never power off or reconfigure, because losing it cuts off the controller itself. Examples are the controller's own PoE port and the site uplink. It is derived from the switch's controller and uplink ports, and extra ports can be declared.
_Avoid_: Reserved port, system port

**Runner**:
A GitHub Actions self-hosted runner that qwe deploys onto a controller and binds to exactly one pool. Its name is derived from that pool. The word "runner" means only this.
_Avoid_: Agent, worker, executor

**Execution backend**:
The way commands reach a target: `local`, `ssh` or `docker`. Commands from `run:` steps and commands that plugins issue both go through it.
_Avoid_: Connection plugin, transport, runner

**Become**:
A step setting for which user its commands run as on the target: a user name, a uid, or `true` meaning root. It needs passwordless sudo on the target, and if sudo refuses, the step fails with reason `become-denied`.
_Avoid_: sudo, run-as, privilege escalation

**Recording backend**:
An execution backend used in tests. It runs nothing: it records every command it's asked to run and returns scripted results, so plugins can be tested without hardware or SSH.
_Avoid_: Mock backend, fake, dry-run

### Plugins

**Plugin**:
LuaJIT code that runs on the operator host. A step plugin does its work by composing commands and running them on a target through the execution backend. It never runs on the target.
_Avoid_: Module, action, extension

**Step plugin**:
A plugin that implements a step. It runs in its own short-lived process for each step, and its only interfaces are its `with:` inputs, its step outputs and its log output. It is the primary kind of plugin.
_Avoid_: Action, task plugin, executor

**Service plugin**:
A plugin that lives inside the engine for the whole workflow run and provides a capability to the engine and to other service plugins, for example an execution backend, the log sink, the secret key source or the inventory reader. Other code reaches it through the engine by name, never by importing it.
_Avoid_: Daemon, provider, extension

**Built-in plugin**:
A plugin compiled into the qwe binary, for example `run`, the execution backends, and the first device and runner plugins.
_Avoid_: Core plugin, stdlib, bundled plugin

**Project plugin**:
A plugin loaded from source out of the project's plugin directory next to the workflow, with no rebuild. It may not have the same name as a built-in plugin.
_Avoid_: User plugin, local plugin (the word "local" means the operator host), extension

**Strict globals** (`qwe.strict`):
The environment a plugin's Lua runs in: reading or writing a global that does not exist is an error, so a typo cannot silently become `nil` or leak a global. It prevents mistakes. It is **not a sandbox**: it proxies the real globals, so a plugin still has `io`, `os.execute`, `loadstring` and everything else the interpreter has.
_Avoid_: Sandbox, isolation, restricted environment

**Trust boundary**:
The workflow directory and the inventory are **trusted input**, in the way a `Makefile` or a `.github/` directory is. Project plugin code runs with the operator's full privileges on the operator host, and a workflow can already run any command through `run:`, so qwe does not try to contain either. Running or validating a workflow directory someone else wrote is running their code as you; read it first. The inventory is trusted the same way: it names the hosts qwe connects to and holds the secrets a job can read.

What `qwe validate` does with a project plugin: it reads the source, runs luacheck over it, checks that it compiles, and checks its `schema.json`, whose `kind` (`check-apply` or `argv`) declares what the module will export. **It runs no plugin code**: not the top level, not `check`, not `apply`. The plugin's top level runs, under strict globals, only when a step uses it (in that step's forked child during `qwe run`), and there the module is checked against its declared kind, failing the step with `plugin-error` if it does not export what it said. Pinned by `tests/e2e/validate_runs_no_plugin_code`. Running a workflow directory you did not write still runs its plugins as you.
_Avoid_: Sandbox, isolation, untrusted plugin
_Avoid_: Sandbox, isolation, untrusted plugin

**Secret**:
A value that must never appear in logs. It comes from an inline-encrypted value in authored YAML or from a step output declared secret, and anything built from a secret is itself secret. Once qwe knows a secret's plaintext, that plaintext is masked in all output for the rest of the workflow run.
_Avoid_: Credential, sensitive value, vault value

**qwe serve**:
A future subcommand, provided by the qwerver component, that will serve the web UI, the API, and a language server (LSP) for editing workflow YAML. It is out of scope for now. Its only commitment is that it will exist as a subcommand of the same binary.
_Avoid_: daemon mode, qwe server
