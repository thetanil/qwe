# qwe — Remote Execution & Inline Secrets

**Status:** Draft / planning. Revised 2026-09 to match `docs/adr/` and `CONTEXT.md`.
**Extends:** *Workflow Kernel — Architectural Design Document* (the base spec). Section references like §10, §12, §17 point to that document.
**Scope:** Two features — (I) executing workflow steps on remote hosts over a cached SSH connection, and (II) inline encrypted secrets in authored YAML.

Both features are deliberately built *without* touching the kernel's core loop. Each lands behind an existing seam (an extension point, the kernel context, the output tee) and reuses subsystems the base spec already defines. That is the point of the design, not a happy accident.

---

## Part I — Remote Execution over SSH

### I.1 This is an execution backend, not a kernel concern

Running a step over SSH is the *same contract* as running it as a local subprocess: supply a command, environment, and stdin; receive a streamed stdout/stderr and an exit code; honor a timeout; be cancellable. Only the transport differs — local uses fork/exec, remote uses a channel on a cached SSH connection.

This resembles Ansible's **connection plugin** model (`local`, `ssh`, `docker`, …) and maps onto the **execution backend**, a kind of service plugin (§12). There is one important difference from Ansible: qwe never runs plugin code on the target. Plugins run on the operator host and only *build commands* (ADR-0002), so a target needs nothing but `sh`.

An execution backend has two jobs, and **starting processes isn't one of them**:

1. **Translation**, a pure function: `(command, env, stdin) → local argv`.
   - `local` → `sh -c '<preamble reader>; exec <cmd>'`
   - `ssh` → `ssh -S <sock> -o ControlMaster=no <dest> -- sh -c '<preamble reader>; exec <cmd>'`
   - `docker` (later) → `docker exec -i <ctr> sh -c '…'`
2. **Connection lifecycle**, if the backend has one (§I.2).

The kernel starts every process itself, through fork/exec, the process group, pipes into the job's ring, timeout and cancel, which is one path for every backend. **The kernel remains entirely SSH-agnostic.**

### I.2 Connection is a service; output rides the existing path

- **The connection is owned by the ssh service plugin, in the parent process only.** It starts a ControlMaster per target before the kernel forks that target's first step, health-checks it with `ssh -O check`, and closes it with `ssh -O exit` when the workflow run ends. Masters are keyed by destination and options.
- **Every master is opened before the first job starts**, one per distinct target the selected jobs use, and never while a job or step timer is armed. The parent blocks on a connect, so doing it inside the event loop makes one slow host delay every other job's timeout — measured at 10 s, against a step that asked for 1 s. Connecting up front keeps the timeout guarantee exact (§I.5) and costs about 306 ms per target, paid before any timer exists. A target that cannot be reached fails its own jobs with reason `unreachable`; the rest of the run proceeds.
- **Only the parent ever creates a master.** A forked step inherits the control socket *path* and only ever runs `ssh -S <sock> -o ControlMaster=no`. A master a child opened itself could be neither seen nor closed by the parent. So what a step receives is the connection's *address*, not a connection object.
- **The master lives outside every step's process group**, in its own session. Otherwise cancelling a step (`kill -pgid`) would kill the connection the next step needs.
- **The ControlPath is short and per run** (`/tmp/qwe-<uid>/<qwe pid>.%C`), because Unix socket paths are limited to about 108 bytes, and test and sandbox temp directories are often longer than that.
- **Command output uses the existing subprocess I/O path** (§10): the per-job ring, redaction, log sink and live consumers, unchanged.
- **There is no event bus** (ADR-0004). Connection problems appear as step reasons (`connection-lost`) and log lines.

### I.3 Decision: host OpenSSH binary via ControlMaster (accepted)

No SSH library is linked into the binary. The `ssh` backend shells out to the **host OpenSSH client**, using ControlMaster / ControlPersist for connection caching — the same mechanism Ansible relies on:

1. The connection-pool plugin ensures a **master connection** is running for a host: `ssh -M -N -o ControlPersist=<ttl> …`, which holds a **control socket** (a Unix domain socket managed by OpenSSH).
2. Each remote command then runs as an **ordinary local subprocess**: `ssh -S <control-socket> -o ControlMaster=no <dest> -- sh -c '…'`. It multiplexes over the existing master, paying the TCP + crypto handshake only once per host. Jumphosts, keys and agent forwarding come from the operator's `~/.ssh/config`: an inventory target names an SSH alias or host, and qwe passes it through unchanged.

The "cached connection" is the control socket; the "channel" between the connection plugin and the executing step is that socket, a well-specified and battle-tested protocol we neither design nor implement.

### I.4 What this reuses, and what it costs

**Reused unchanged** — because from the kernel's point of view a remote step *is* just a local subprocess:

- the epoll event loop and its three event sources (§5.1),
- the per-job ring buffer, backpressure, and drop accounting (§10),
- job timeouts and the `terminating` teardown path (§7, §9),
- process-group cancellation (§9.3).

**Inherited from OpenSSH for free:** crypto and cipher negotiation, host-key verification, `~/.ssh/config` parsing, agent forwarding, and file transfer via `scp`/`sftp` over the same control socket.

**Costs / accepted trade-offs:**

- A cap on concurrent sessions per target: 8 by default, set per target with `max-sessions:` in the inventory. OpenSSH's server `MaxSessions` defaults to 10 per multiplexed connection, and the 11th session fails with an unhelpful error. Jobs above the cap wait for a slot instead of failing.
- One `ssh` fork/exec per remote command. This is cheap — ControlPersist already eliminated the expensive part (the handshake); what remains is spawning a lightweight client that attaches to an existing socket.
- A hard dependency on the host OpenSSH binary being present. This is the same bet Ansible makes and is acceptable for a Linux lab workflow engine. (`dbclient` from Dropbear is a possible lighter substitute in constrained environments, selectable by the backend.)

### I.5 Cancellation & timeouts over SSH

**Timing guarantee.** A step or job timeout fires within **100 ms** of its deadline, whatever else the run is doing. The event loop itself is far better than that — a 1.000 s timeout was measured firing at 0.996 s, unmoved by 40 MB of concurrent output — so the budget exists to bound the parent's ssh work, not the loop. It holds because masters are opened before any timer is armed (§I.2), the remote kill is fire-and-forget, and every remaining parent-side ssh call is bounded. **The one exception** is a mid-run reconnect after a master dies: that blocks for up to 3 s, so other jobs' timers may be late by that much on that error path, and no other.

Cancellation uses the existing teardown: the local `ssh -S` client is signalled with the step's process group (§9.3). That alone is not enough: sshd tells a command run without a tty nothing when its channel closes, so the remote processes would keep running. So every remote step carries `QWE_STEP=<token>` in its environment (children inherit it), and on TERM or KILL the parent also runs a short `sh` over the master that signals every process whose `/proc/<pid>/environ` has that token. It needs `/proc`, `tr` and `grep` on the target. A process that escapes by scrubbing its environment is not found: the remote counterpart of the §9.3 known gap. Steps use `-o ProxyCommand=false`, so a client whose master died fails (`connection-lost`) instead of opening a connection of its own. Step timeouts, job timeouts and operator cancel use the same teardown path as any local step (§7.3, §9).

If the master dies partway through a step, that step's `ssh -S` fails, and the step is `failed` with reason `connection-lost`. The parent re-establishes the master before the next step. **No step is ever retried automatically**, because it might not be idempotent.

**What counts as a lost connection** is two facts together: the local client exited **255**, ssh's own error status, *and* `ssh -O check` finds no master. Exit 255 alone is not enough, because a remote command is free to exit 255 itself; asking the master settles which it was. This applies to `run:` steps. A plugin step that loses its connection fails as `plugin-error`, because the plugin is what saw the failure and the engine has only its result to go on.

**If the master cannot be started at all** — the host is unreachable, the key is refused, `ConnectTimeout` expires — the step fails with reason **`unreachable`**, and ssh's own message goes to the job's log. It is not `connection-lost`: nothing was connected to lose. Nor is it `engine-error`, which is reserved for faults in qwe itself (a failed `fork`, a log or timer it could not open). A host being down is a fact about the world, and a lab tool has to report it as one.

The three cases read differently on purpose:

| Situation | Reason |
|---|---|
| Never reached it: the master could not be started | `unreachable` |
| Was connected, the master died under a running step | `connection-lost` |
| Was connected, the master died, the bounded reconnect then failed | `unreachable` |

### I.6 Deferred: in-process SSH (libssh2)

Linking **libssh2** (3-clause BSD, client-only, supports non-blocking operation with an app-owned socket) and driving it directly from the event loop remains a future option. It is deferred because:

- It would require a **new kernel primitive**: *plugins that own fds must be able to register them, with readable/writable callbacks, into the event loop.* This is a legitimate, generalizable capability (any plugin doing its own network or DB I/O would want it), which is exactly why it belongs in §17 as an open question rather than being smuggled in for SSH alone.
- Even non-blocking, the crypto **handshake/auth is CPU-bound** and can stall the single thread for a beat; establishment would have to happen out-of-band, with only *established* channels multiplexed in the loop.
- libssh2 lacks some features (e.g. GSS-API, certificate auth).

Only pursue it given a concrete driver: eliminating the per-step `ssh` fork/exec, running where no OpenSSH binary exists, or needing programmatic in-process control of channels/tunnels/SFTP. If pursued, the stack would be **libssh2 + a permissive crypto backend**; but that decision is out of scope while the host binary is accepted.

### I.7 Env and stdin: the preamble

ssh doesn't forward environment variables (unless the server has `AcceptEnv`), and writing `env VAR=… sh -c …` into the remote command line would put values, including secrets, into argv on both hosts. So **all declared env** (workflow, then job, then step, innermost wins, as in GitHub Actions) reaches the command through a **preamble on stdin**. The remote `sh` reads exactly the preamble, exports it, and then `exec`s the command, which inherits **the rest of stdin byte for byte**. This matters for steps such as `install -m 600 /dev/stdin <path>`, which receive a secret as their stdin. The `local` backend uses the same preamble, so there is one code path to test.

Step outputs from `run:` steps come back through a `$QWE_OUTPUT` file on the target, which the backend reads and deletes after the command exits. They never travel in stdout (ADR-0005).

### I.8 `become`

A step may run as another user on its target: `become: true` means root, a string is a user name, and an integer is a uid (`0` is uid 0, distinct from `false`). The backend translates this into `sudo -n` / `sudo -n -u <name>` / `sudo -n -u '#<uid>'`. `sudo` wraps **the shell that reads the preamble**, not the other way round, because sudo's `env_reset` would otherwise drop the declared env. `-n` makes sudo fail immediately rather than wait for a password, and that failure is reported as `failed` with reason `become-denied`. Passwordless sudo on the target is a manual prerequisite. A sudo password delivered through the preamble (`sudo -S`) is possible later if a target needs it.

---

## Part II — Inline Encrypted Secrets

### II.1 Rationale

Secrets are encrypted inline in the authored YAML. GitHub Actions deliberately does not do this, because in a cloud/shared-runner context there is nowhere trustworthy to hold the decryption key. **qwe is a local lab engine**, so that objection does not apply: the key can live on the same machine as the operator. Ansible Vault proves the model.

### II.2 Decision: libsodium with a raw key file (accepted)

- **Library: libsodium** (ISC license). Purpose-built, misuse-resistant, far lighter surface than a TLS stack. mbedTLS was only attractive while it could double as the in-process SSH/TLS backend; with SSH now the host binary (Part I), nothing else justifies pulling in a TLS stack, so libsodium is the right tool for secrets alone.
- **AEAD: XChaCha20-Poly1305** (`crypto_aead_xchacha20poly1305_ietf_*`). Authenticated encryption — confidentiality *and* tamper detection — with a 24-byte random nonce large enough to generate randomly without a counter.
- **Key model: raw key, not passphrase.** The key file holds a 32-byte random key used directly; there is **no password KDF**. This is deliberate: a raw machine-local key removes the entire "did we pick enough Argon2/PBKDF2 iterations" question that a human passphrase would force. (Per-purpose subkeys, if ever wanted, can be derived with `crypto_kdf_derive_from_key` — but that is an option, not a requirement.)
- **CSPRNG:** libsodium `randombytes` for nonces (and any salts).
- **Memory hygiene:** `sodium_memzero` on plaintext buffers after use. This reaches the C buffers — the key, a decrypted plaintext, `qwe encrypt`'s input — and **not** the copies Lua holds, because a Lua string is immutable, interned and collected whenever the GC decides. A decrypted secret therefore exists as a Lua string in the parent for the rest of the run. Closing that would mean keeping plaintext out of Lua entirely, which the templating design does not allow today; it is a known limit, recorded rather than solved.
- **The secret CBOR tag is 32768**, the first value in RFC 8949's first-come-first-served range. It is defined once, in `src/edge/yaml/secret_tag.h`, and shared by the C transcoder and `qwe.cbor`, so the two can never disagree about what marks a secret.
- **A secret satisfies a `with:` schema that says `type: string`** with no custom keyword and no placeholder, because by the time the schema runs, the value *is* a string: `${{ secrets.X }}` is text in the document, and substitution happens later, in the parent, just before the step. A bare `!encrypted` envelope in `with:` is refused outright — an envelope is only ever a value in a `secrets:` map.

### II.3 Key material & file

- **Location:** `~/.config/qwe/secret`.
- **Contents:** a single 32-byte AEAD key (stored raw or base64; generated by `qwe keygen` via `crypto_aead_xchacha20poly1305_ietf_keygen`, which refuses to overwrite an existing key).
- **Hygiene, enforced at load:**
  - Refuse a key file that is group- or world-readable (the same check `ssh` performs on private keys); require `0600`.
  - Prefer the file over an environment variable for the master key. Env vars leak into child processes and are world-readable via `/proc/<pid>/environ`; a `0600` file does not. (An env-var key source may still exist as a pluggable option — see II.6 — but the file is the default and the recommendation.)

### II.4 The envelope format

An encrypted value is stored as a **versioned envelope**, so algorithms can rotate later without breaking existing files:

```
version ∥ algorithm-id ∥ nonce ∥ ciphertext ∥ auth-tag
```

No KDF parameters are stored, because the raw-key model has no KDF. Versioning everything follows Ansible Vault's `;1.1;AES256`-style header precedent. Authoring is supported by **`qwe encrypt`** (analogous to `ansible-vault encrypt_string`), which reads a plaintext **from stdin, never from an argument**, and emits the envelope string the user pastes into YAML — decryption alone would be useless without a blessed way to *produce* ciphertext.

**The text form**, which is what `qwe encrypt` prints and what an `!encrypted` value holds, is:

```
qwe:1:xchacha20poly1305:<base64 of nonce ∥ ciphertext ∥ auth-tag>
```

The version and algorithm id are not decoration: they are passed to the AEAD as its **additional data**, so editing either one fails authentication rather than selecting a different algorithm. A reader can therefore tell a qwe envelope from any other string, and refuse one it does not understand, without holding a key — which is what lets `qwe validate` check that every `secrets:` entry is a well-formed envelope on a machine with no key file at all.

`qwe encrypt` drops one trailing newline from its input, so `echo hunter2 | qwe encrypt` seals `hunter2` rather than `hunter2\n`. Use `printf %s` when the exact bytes matter.

### II.5 Encrypted values are a tagged type, decrypted late

The cipher is roughly 10% of the work; the other 90% is keeping plaintext from leaking through the machinery the base spec already defines. The governing rule is **decrypt late, not at load**:

- **Secrets are declared by name** in a `secrets:` map, in the workflow or the inventory (the switch password belongs to the controller, so it goes in the inventory). An `!encrypted` envelope is allowed **only** as a value in a `secrets:` map. The same name defined in both is a validation error, never a silent override.
- **They're referenced as in GitHub Actions:** `${{ secrets.NAME }}`. This is allowed in `env:` and `with:` values. It's a **validation error inside `run:` script text**, because substituting a secret into the script puts it into `sh -c` argv on both hosts. The error suggests `env:` instead. GitHub only advises against this. qwe can enforce it because the tag marks what's secret.
- An encrypted value is a **distinct tagged type**, a CBOR tag meaning "encrypted secret", and is **never** a plain string. Anything built from a secret (a `with:` string that substitutes one) is itself secret as a whole.
- The **YAML→CBOR transcoder (§15) preserves the envelope and doesn't decrypt.** The value stays encrypted through transcoding and through the workflow representation.
- **Decryption happens just in time, in the parent**, as a step's env and inputs are materialized, *before* the step is forked. It's done by a **secrets service** (II.6) that holds the key. It has to be the parent, because the parent runs the tee that redacts (II.7) and must know every plaintext in use.

This mirrors §2.1's layering exactly: the core needs only a primitive `decrypt(blob, key) → plaintext`; the *key source* is the rich, evolving, pluggable peripheral.

### II.6 The secrets service & pluggable key source

A **secrets service**, registered in the kernel context, owns the key and performs decryption on demand. The **key source** is pluggable behind it: the default is the `~/.config/qwe/secret` file; later sources (env var, an agent, a KMS) are additional plugins that satisfy the same interface — without any change to the core `decrypt` primitive. This is the same primitive-trusted-core / rich-peripheral split used for config bootstrap and the log sink.

### II.7 Redaction at the tee (interaction with §10)

Encrypting the value at rest is worthless if a subprocess echoes the plaintext into the logs, which are the source of truth (§10). Therefore:

- The secrets service maintains the **run-wide set of known plaintext values**. A decrypted `secrets:` value joins it when it's first materialized for a step. A **secret step output** (II.9) joins it as soon as the parent receives it. Nothing leaves the set before the run ends, so a secret is masked in every later step's output too, not only in the step that used it.
- **Redaction is a transform at the output tee** (§10.4): as bytes are drained from the per-job ring toward the log sink and any live consumers, occurrences of known secret values are replaced with `***`. Matching must work when a secret is split across two reads.
- This must happen **at the tee point, before** bytes reach the log sink or any consumer feed — matching GitHub Actions' masking model. A secret written to stdout by the child must be masked on the way in, or it lands permanently in the source-of-truth log.

### II.8 Secrets never go in argv

Command-line arguments are world-readable via `/proc/<pid>/cmdline`. Decrypted secrets reach a step **only through the stdin preamble (as env) or the command's own stdin, never as arguments** (I.7). The validation rule in II.5 enforces this for `run:` text, and the backends enforce it for everything they translate.

**The one exception** is at the `ntgrrc` boundary on the controller (ADR-0006). `ntgrrc login` accepts a password only through `--password` or a TTY prompt, and runners have to be able to log in again on their own. So qwe sends the switch password over stdin into a mode-0600 file on the controller, and installs a controller-side wrapper that reads that file and calls `ntgrrc login --password …`. The password is in argv only on the controller, only for the duration of the login, and never in any command line qwe builds.

### II.9 Secret step outputs

A step may produce a value that is itself secret. For example, `github.runner-token`, running `on: local` with the operator's GitHub credential, produces a short-lived runner registration token for a later step on the controller to use. Such outputs are **declared** secret, in the plugin's output schema (`secret: true`) or with `secret-outputs:` on a `run:` step. They travel on the output channel, never in stdout (ADR-0005). Their plaintext joins the redaction set before any later byte is logged, and they never appear in `result.json`. They're readable only by later steps in the same job.

---

## Dependency additions

| Dependency | Role | License | Notes |
|---|---|---|---|
| **libsodium** | Secrets: AEAD, CSPRNG, memory zeroing, (optional) subkey derivation | ISC | Vendorable, C, permissive. Only new linked dependency introduced by this extension. |
| Host **OpenSSH** binary | Remote execution transport | — | Not linked; invoked as a subprocess. Assumed present. |

Explicitly **not** added: no SSH library (libssh2/libssh), no TLS stack (mbedTLS/OpenSSL). The remote-execution feature adds *zero* linked dependencies.

---

## Additions to base §17 (Open Questions)

1. **Plugin-registered fds in the event loop.** Required only if in-process SSH (I.6) is ever pursued; generalizes to any plugin doing its own I/O. The single genuinely kernel-touching consequence of either feature.
2. **Detached remote processes** surviving channel close (I.5) — whether qwe needs an async/poll-style reaper, as Ansible does.
3. **Secret value granularity for redaction** (II.7) — exact-match on full plaintext values is the baseline; whether to also mask substrings/encodings (base64 of a secret, etc.) is open, and over-masking has its own failure modes.
4. **Key rotation workflow** — the envelope is versioned (II.4), but the operator-facing re-encrypt/rotate flow is unspecified.
5. **`sudo` password via the preamble** (I.8). Not needed while targets have passwordless sudo.

---

## Decision Log Addendum

- **Remote execution = an execution-backend extension point;** `ssh` is a plugin, kernel stays SSH-agnostic.
- **Transport = host OpenSSH via ControlMaster/ControlPersist** (Ansible model). No SSH library linked; a remote step is just a local `ssh -S` subprocess, reusing loop/ring/timeout/cancel unchanged.
- **A backend = a pure argv translation + a connection lifecycle;** the kernel does all process spawning.
- **ControlMasters are created only by the parent**, outside every step's process group, with a short ControlPath. Steps use `-o ControlMaster=no`. `connection-lost` fails the step, and nothing is retried.
- **Per-target session cap of 8** (below OpenSSH's `MaxSessions` 10).
- **Env and secrets reach commands through a stdin preamble** that passes the rest of stdin through exactly. **`become`** wraps the preamble shell with `sudo -n`.
- **No event bus;** output uses the existing ring path.
- **In-process libssh2 deferred;** would require a new "plugins register fds into the loop" kernel primitive.
- **Inline secrets accepted** — justified because qwe is local, unlike GHA.
- **Crypto = libsodium**, XChaCha20-Poly1305 AEAD, **raw 32-byte key** (no passphrase, no KDF), nonces from `randombytes`.
- **Key file = `~/.config/qwe/secret`**, `0600` enforced, preferred over env.
- **Encrypted value = tagged CBOR type; decrypt late** at env materialization via a secrets service with a **pluggable key source.**
- **Redaction at the output tee** (§10) before logs/consumers; **secrets via env/stdin, never argv.**
- **Envelope is versioned** (`version ∥ alg-id ∥ nonce ∥ ciphertext ∥ tag`); `qwe encrypt` produces it from stdin.
- **Secrets are named** in `secrets:` maps (workflow or inventory; duplicates are an error) and referenced as `${{ secrets.NAME }}`. **Forbidden in `run:` text.**
- **Decryption happens in the parent** before the fork. The redaction set covers the whole run and includes **secret step outputs**.
- **The one argv exception** is `ntgrrc login` on the controller (ADR-0006).
