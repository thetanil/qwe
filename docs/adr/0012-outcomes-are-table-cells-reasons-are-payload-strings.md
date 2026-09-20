# Outcomes are table cells; reasons are payload strings

Every finished job and step carries an **outcome** (`success | failed |
skipped | cancelled`) and a **reason** saying why. The two look alike in
`result.json` and in the trace, and they are not alike at all in what they cost
to add.

An **outcome** is a state in the lifecycle table (ADR-0010). Reaching it for a
new cause means a new event column, a cell in every state's row, and the
four-layer test treatment, because the table's design point is that every cell
is written out and an unwritten cell aborts.

A **reason** can be either. The table can attach a fixed one — `R_DEP`,
`R_CANCEL`, `R_TIMEOUT`, `R_ENGINE` — or a cell can be marked `R_EVENT` and
take an arbitrary string from the event payload, set by the parent. Two cells
are `R_EVENT` today, both on `leader-exit-fail`, and that is how `exit-code`,
`not-converged`, `plugin-error`, `become-denied` and `connection-lost` all
reach `result.json` without ever appearing in the table.

**So: reporting a new *cause* of an existing outcome is free, and reporting a
new outcome is a table change.** That asymmetry decides the cheap design of
most "how should this be reported" questions, and it is not obvious from
reading either the table or `result.json`.

The worked example is two tickets that look identical and are not. An
unreachable ssh target (`m1-review/06`) wants `failed`, so its job spawns a
step, the step's `ssh -S` exits 255 for real, and the parent attaches
`unreachable` on the `leader-exit-fail` payload. No table change. A target
disabled in the inventory (`m1-review/10`) wants `skipped`, so the run stays
green while hardware is out for repair — and every path to `skipped` has a
fixed reason with no event meaning "out of service". Table change. The
difference is the outcome, not the reason.

The second rule follows from the first being abused. `engine-error` is the
table's reason on the `start-failed` path, which made it the path of least
resistance for anything that went wrong before a step ran — including an
unreachable host. That is wrong. **`engine-error` means a fault in qwe itself**:
a failed `fork`, a log or timer it could not open, a ring it could not
allocate. A host being down, a connection dropping, sudo refusing — those are
conditions in the world, and an operator reading `engine-error` for one of them
will go looking for a bug in the engine. Conditions in the world get their own
reason, which costs nothing.

## Consequences

- Adding a reason is a one-line change in the parent and a line of
  documentation. Adding an outcome-cause pairing is a table change with tests,
  and should be weighed accordingly — including whether a second future use
  would justify the same new event.
- A reason set from the payload is set by the **parent**, which is trusted.
  Reasons arriving from a forked child are whitelisted in `read_step_msg`, so a
  plugin still cannot invent an outcome for the engine.
- `engine-error` is now narrow enough to be a signal: if it appears, something
  is wrong with qwe, and the run is worth reporting as a bug.
- The `R_EVENT` path needs a real event. A cause that never spawns a step
  cannot use it, which is the constraint that pushes such cases toward a table
  change rather than a cheap string.
- The full reason list and what each one means stays in `CONTEXT.md` and
  `docs/qwe-ssh-sec.md`. This ADR records the rule, not the table.
