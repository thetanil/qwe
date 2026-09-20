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

What was promoted out of `m1-engine` when it was archived: ADR-0011
(`become-denied` is detected by a probe), the session-cap rule and the
validation order in design §8.3 and §14, and the envelope text form, the
`connection-lost` definition, the failed-master-start behaviour, the secret tag
number and the Lua memory-zeroing limit in qwe-ssh-sec I.5, II.2 and II.4.
