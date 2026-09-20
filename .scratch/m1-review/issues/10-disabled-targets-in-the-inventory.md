# 10: Mark a target out of service in the inventory

Status: resolved
Category: enhancement
Type: task
Blocked by: 06

## What

A lab has hardware that goes away: a controller out for repair, a device
unplugged, a host being reimaged. Today the inventory has no way to say so. The
operator's choices are to delete the target (and every job that names it), or
to leave it and watch those jobs fail on every run.

This came out of triaging `m1-review/06`. Pre-connect makes an unreachable
target fail its own jobs with reason `unreachable` and a non-zero exit, which
is right when the host is *supposed* to be up. It is wrong when the operator
already knows it is down: every run goes red for a known reason, and people
stop reading the colour.

The flag carries its reason as a string, so the inventory documents itself and
the reason can travel into the log and `result.json`:

```yaml
targets:
  pi03:
    backend: ssh
    host: pi03
    disabled: "in for repair, 2026-09"
```

## The constraint from ticket 06

A disabled target and an unreachable target **align on scheduling** and
**differ on outcome**. Both take that target's jobs out of the run, and in both
cases dependents skip with `dependency-failed` through the identical join rule.
They differ in what the operator is told:

| | Outcome | Reason | Run exits |
|---|---|---|---|
| Unreachable (06) | `failed` | `unreachable` | non-zero |
| Disabled (this) | `skipped` | `target-disabled` | **0**, if nothing else failed |

An unreachable host is a fault and should be loud. A disabled host is a
declared state and should be quiet. `qwe_run_workflow` already treats a run as
ok when every job is `success` **or** `skipped`, so the green run falls out for
free once the outcome is `skipped`.

Green must not mean silent, though. When any job was skipped for a disabled
target, the run prints one line at the end naming how many and which targets,
so a run that did a fraction of the work cannot look identical to a full one.
`result.json` needs nothing extra: the per-job outcome and reason already carry
it.

## What it costs, and why it costs more than ticket 06 did

**The expensive part is the outcome, not the reason** (ADR-0012). This looks
like the same shape of problem ticket 06 solved for free, and it is not.

Ticket 06 needed a new **reason** on an existing outcome. A job on an
unreachable target ends up `failed`, it really does spawn a step, that step's
`ssh -S` really does exit 255, and the `leader-exit-fail` cell is already
`R_EVENT` — so the parent attaches the string `unreachable` and nothing in the
table moves.

This ticket needs a new **outcome** for a new cause. The whole point is that a
disabled host must *not* turn the run red, so the job has to end `skipped`, and
there is no way to spawn your way there: spawning a doomed step gets you
`failed`. The lifecycle table has exactly three paths to `SKIPPED` —
`needs-failed` from `pending`, and `cancel` from `pending` or `ready` — each
with a fixed reason, and no event that means "do not run this job".

The two cheap escapes are both lies. Reusing `needs-failed` reports
`dependency-failed` when no dependency failed. Reusing `cancel` reports
`cancel-requested` when nobody cancelled anything.

### Add one generic event, not a target-specific one

Per ADR-0012, the outcome path is what costs; reasons are free once a cell is
`R_EVENT`. So add **one** event — `skip` — whose reason comes from the payload,
rather than a `target-disabled` event that can never serve anything else:

| State | Cell for `skip` |
|---|---|
| `pending` | → `SKIPPED`, `R_EVENT`, actions `REC_JOB, END` |
| `ready` | → `SKIPPED`, `R_EVENT`, actions `REC_JOB, END` |
| every other state (19 of them) | `X` — a job that has started cannot be skipped |

That is the whole table change: one column, two transitions, nineteen
impossible cells, with the four-layer treatment ticket 17 established.

**It has a second user already on the roadmap.** Design §7 says "Further
conditions in the GitHub Actions family (`if:`, `failure()`, expressions) are
deferred", and §17's open question 4 defers `if:` again. A job skipped by
`if: false` needs exactly this: `SKIPPED` with a reason that is neither
`dependency-failed` nor `cancel-requested`. Building the event generically here
means `if:` inherits it and pays nothing.

## Acceptance criteria

- [x] A job whose target is disabled is `skipped` with reason `target-disabled`, and its dependents skip with `dependency-failed`. `e2e: tests/e2e/inventory_target_disabled/`
- [x] A run whose only non-success jobs were skipped for a disabled target exits 0. `e2e: tests/e2e/inventory_target_disabled/`
- [x] That run prints one line naming how many jobs were skipped and which targets were disabled, so a nearly-empty green run is visibly different from a full one. `e2e: tests/e2e/inventory_target_disabled/`
- [x] Unrelated jobs in the same run still run and still succeed. `e2e: tests/e2e/inventory_target_disabled/`
- [x] The `disabled:` string appears as the job's reason detail in `result.json`, so the operator learns why without opening the inventory. `e2e: tests/e2e/inventory_target_disabled/`
- [x] No master is opened to a disabled target during pre-connect, and a disabled target that is also unreachable still reports `target-disabled`, not `unreachable`. `e2e: tests/e2e/inventory_target_disabled_not_contacted/`
- [x] `qwe validate` is silent about disabled targets: a workflow whose every job targets disabled hardware validates and exits 0. `e2e: tests/e2e/validate_ignores_disabled/`
- [x] `disabled:` must be a non-empty string; `disabled: true` is a validation error naming the expected shape, at the value's position. `e2e: tests/e2e/inventory_disabled_must_be_a_reason/`
- [x] The new `skip` event is a full column in the table: two transitions and nineteen `X` cells, with no `UNSET` left. `unit: src/kernel/lifecycle_test.c::every_cell_is_written` (existing, must stay green), `src/kernel/lifecycle_model.c`
- [x] A `skip` event is impossible once a job has started, and the table aborts if one arrives. `unit: src/kernel/lifecycle_test.c::skip_after_start_is_impossible`
- [x] The event carries its reason from the payload, so a future `if:` can reuse it without touching the table. `unit: src/kernel/lifecycle_test.c::skip_reason_comes_from_the_payload`
- [x] `disabled:` and the `target-disabled` reason are documented in the inventory section of design §13 and in the reason list in §7.

## Comments

> *This was generated by AI during triage.*

Triaged 2026-09-20, in two sittings.

The first was during the `m1-review/06` grilling, which settled only the
cross-ticket constraint: align on scheduling, diverge on outcome. The
exit-code semantics were proposed there and not confirmed, which is why this
ticket stayed `needs-triage` afterwards rather than being done.

The second closed the remaining four questions:

- **A green run, plus a summary line.** Exit 0 is what keeps red meaningful
  while hardware is away. The summary line is the guard against the failure
  mode of that choice — a run doing a fraction of the work looking identical to
  a full one.
- **A string, not a boolean.** `disabled: "in for repair"` documents itself in
  the inventory and gives the log and `result.json` something to show. A
  boolean pushes the reason into a YAML comment no tooling can read, for no
  saving.
- **`qwe validate` stays silent.** The workflow is valid; a disabled host is a
  fact about the world, not a defect in the document. Warning would mean
  inventing a warning channel `validate` does not have, and failing would make
  a valid workflow unvalidatable because of transient lab state.
- **The table change is justified, and not by this ticket alone.** `if:` is
  deferred in design §7 and §17, and needs the identical event. Making the
  event generic (`skip` with a payload reason) means it is built once.

## Agent Brief

**Category:** enhancement

**Summary:** Let the inventory mark a target out of service with a reason
string. Jobs on it are `skipped` with reason `target-disabled`, their
dependents skip as usual, unrelated jobs run, and the run exits 0 with a line
saying what was skipped.

**Current behavior:**
No way to express it. A target that is down either gets deleted from the
inventory along with the jobs that name it, or fails those jobs on every run —
after `m1-review/06`, with reason `unreachable` and a non-zero exit.

**Desired behavior:**
`disabled: "<reason>"` on a target. Its jobs never start, never get contacted
during pre-connect, and end `skipped` / `target-disabled`. The run is green if
nothing else failed, and says how many jobs it skipped and why.

**Key interfaces:**
- `inventory.schema.json` gains `disabled` on the target definition: a string,
  `minLength: 1`. `qwe.inventory` gains an accessor beside `host` and
  `max_sessions`.
- The lifecycle table gains one event column, `skip`, with `R_EVENT` cells from
  `pending` and `ready` and `X` everywhere else. `lifecycle_model.c` must agree
  about when it can occur.
- `run_all` sends `skip` to every job whose target is disabled, before the
  first scheduling pass, with the inventory's reason string as the payload.
- The pre-connect phase from `m1-review/06` skips disabled targets entirely.
- The end-of-run summary line goes beside the existing `result.json` write in
  `qwe_run_workflow`.

**Acceptance criteria:** the checklist in this ticket's Acceptance criteria
section.

**Out of scope:**
- `if:` on jobs or steps. The event is built so `if:` can reuse it; the
  condition language, expression evaluation and `always`/`failure()` are all
  still deferred (design §7, §17).
- Disabling anything other than a target — a device, a pool, a single job.
- Any change to how an *unreachable* target behaves; that is `m1-review/06`.

Resolved 2026-09-20.

- Lifecycle: one new event column `skip` (`QWE_LC_EV_SKIP`), `R_EVENT` cells in `pending` and `ready`, `X` in the other 19 states; the model lets it occur in those two states; three layer-4 tests (`skip_reason_comes_from_the_payload`, `skip_column_is_complete`, `skip_after_start_is_impossible`) plus two scenarios. The layer-2 rule "only leader-exit-fail takes the event's reason" now allows `skip`. ADR-0010 mentions the new event. In the test file the event is written `QWE_LC_EV_SKIP` in full because greatest.h defines `SKIP`.
- `qwe.inventory.disabled(target)`; schema `disabled: {type: string, minLength: 1}`; `disabled: true` reports "disabled: must be a non-empty string saying why the target is out of service…" at the value.
- Engine: `note_disabled` (before pre-connect, so it is not contacted) and `skip_disabled` (first thing in `run_all`, before any scheduling pass). `result.json` gains an optional `"detail"` on a job, written only when set, so every other golden is unchanged. The summary line counts jobs skipped for a disabled target (not their dependents) and lists `target (note)` for each, on stderr, after `result.json` is written.
- The ticket's text says "`result.json` needs nothing extra" while its criteria ask for the note to appear as the reason detail there; the criteria won, hence `detail`.
- `validate_ignores_disabled` needed no code: validation never looked at the field.
