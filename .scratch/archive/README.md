# Archived features

Closed features move here, one directory each, keeping their original layout
(`<feature>/spec.md`, `<feature>/issues/NN-<slug>.md`).

They are one level deeper than a live feature on purpose: the frontier scan
globs `.scratch/*/issues/*.md`, which does not reach `.scratch/archive/*/issues/`.
Nothing here is ever picked up as work, and nothing here should be edited — if
something in an archived ticket turns out to be wrong or unfinished, open a new
ticket in a live feature and say so there.

## Why these are kept rather than deleted

**Traceability.** Every acceptance criterion names the test that proves it
(`e2e: tests/e2e/<case>/`, `unit: <file>::<case>`). Across the archived
features that is a requirement-to-test map built while the code was written,
against tests that still exist. Reconstructing it later means re-deriving every
link by hand. A safety-certification effort asks for exactly this artifact.

**What was decided and why.** Each closed ticket's `## Comments` records what
the ticket left open and how it was settled, including the alternatives that
were rejected and the things found by a failing test rather than by design.

A decision that is still *load-bearing* does not belong here, though. Before a
feature is archived, its durable decisions are promoted to `docs/adr/`,
`CONTEXT.md` or the design docs, because those are what an agent reads.
An archived ticket is the record of how the project got somewhere, not the
statement of where it is.

## Archived so far

| Feature | Closed | What it built |
|---|---|---|
| `m1-engine` | 2026-09-20 | The M1 workflow engine: transcoder, schema validation, the job lifecycle table, steps, timeouts, cancellation, parallelism, plugins, check/apply, env and templating, the inventory, the ssh backend, `become`, secrets and redaction, `--job` selection. 21 tickets, 160 acceptance criteria. |
| `m1-review` | 2026-09-20 | A review of the finished M1 engine, and the fixes it found: duplicate YAML keys, `timeout-seconds` and a total conversion, the `$QWE_OUTPUT` file's mode, the ControlPath directory's owner, a master outliving a killed run, blocking ssh calls in the event loop, a dag check that approved when it could not run, the transcoder's trust in libyaml, the plugin trust boundary, disabled targets, and validation executing plugin code. 11 tickets, 74 acceptance criteria. |
| `quality` | 2026-09-20 | Making the C trustworthy: every exit path frees, every allocation is checked (`alloc.h`, and an audit of the bare calls), ASan/LSan, UBSan and Valgrind gates, coverage measurement for C and Lua with a ratchet, fuzzing the YAML edge, OOM injection over validate and run, and closing the C coverage gaps. 10 tickets, 64 acceptance criteria. |

What was promoted out of `quality` when it was archived: nothing new. Its decisions were written as each ticket closed, into `docs/ci-checks.md` (the commands and cadence CI should run), `docs/sanitizers.md`, `docs/valgrind.md`, `docs/fuzzing.md` and `docs/coverage.md` (how coverage is measured, and why each remaining miss is left), and the allocation policy in `src/kernel/alloc.h`. The pointer from `src/kernel/alloc_audit.txt` was repointed to the archived ticket.

What was promoted out of `m1-review` when it was archived: **ADR-0013** (a plugin
file has no top-level code, and validation reads it rather than running it — the
decision ticket 11 made, including the `kind:` attempt that was reverted); four
bullets in the qwe-ssh-sec Decision Log Addendum (pre-connect and the 100 ms
timeout guarantee with its 3 s reconnect exception, the `ControlPersist` ttl as a
backstop, the verified ControlPath directory, and the `0600` `$QWE_OUTPUT` file).
Most of this feature's decisions were already written into the docs as each
ticket closed — design §14 (the timeout unit, maximum and rounding), §15.4
(duplicate mapping keys), §13 and §7 (`disabled:` and `target-disabled`), §12
(plugin shape), qwe-ssh-sec I.2, I.3, I.5 and II.9, and `CONTEXT.md`'s **Strict
globals** and **Trust boundary** entries — which is why the promotion at archive
time was small. Two repairs went with it: `CONTEXT.md` still said that
*validating* someone else's workflow directory runs their code, which ticket 11
had made untrue, and ticket 05's edit to qwe-ssh-sec I.3 had swallowed the
opening of that section's second list item.

What was promoted out of `m1-engine` when it was archived: ADR-0011
(`become-denied` is detected by a probe), the session-cap rule and the
validation order in design §8.3 and §14, and the envelope text form, the
`connection-lost` definition, the failed-master-start behaviour, the secret tag
number and the Lua memory-zeroing limit in qwe-ssh-sec I.5, II.2 and II.4.
