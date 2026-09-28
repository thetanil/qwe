# ISO 26262 tool qualification

Status: ready-for-agent

## Premise

qwe is to be certified as a **general-purpose workflow tool** under ISO 26262,
for work on automotive items up to **ASIL D**. It is a **software tool**
(ISO 26262-8 clause 11), not part of the item. The reference use cases are
control, configuration, flashing, remote test execution and reporting, on
devices under test (DuT: small ARM boards, Raspberry Pi class).

The decision (draft) is
`docs/adr/0015-qwe-is-qualified-for-tcl3-asil-d-by-validation.md`:

- **The safety-rated scope is the kernel and the built-in plugins.** It is
  qualified for **TCL3 at ASIL D by validation (1c)**. The certificate then
  does not depend on any user's context.
- **check → apply → check** is a tool requirement and the core of what is
  validated. check decides whether apply is needed. After apply, check runs
  again, and a second check that still needs a change fails the step
  (`not-converged`). It is built into every plugin and cannot be switched off.
  It does not by itself justify a lower TCL: the verifying check shares its
  author, channel and target resolution with apply.
- **Every plugin's check passes the no-op-apply test.** This is part of
  developing any plugin, although only built-in plugins are rated.
- **`run:` steps and project plugins are outside the claim.** The safety manual
  says the user takes responsibility for them. The current user does so with an
  independent downstream gate over the same requirements (A1) and the raw
  results in a long-term archive as the determinative record (A2).

## History

Split out of `.scratch/sca-round3` on 2026-09-26. That feature's ticket 01 (the
standard and the classification) became this feature's 01. Its ticket 30 (1d and
MISRA) became a fallback ticket here, which ADR-0015 then replaced with
validation (07). sca-round3 is code quality and goes ahead on its own. Per the
ADR, its results are supporting evidence in the qualification report, not a
gate. Nothing in either feature blocks the other.

## Order

1. **03**, **04**: fix the two gaps in the verifying check (found while drafting
   the ADR). They are bugs whatever the ADR says, so they do not wait for 01.
2. **05**: the no-op-apply harness, for every built-in plugin and for project
   plugins.
3. **01** (human): accept the ADR, and ask the certifier whether 1c alone
   suffices.
4. **02**: the tool requirements and the tool safety manual.
5. **07**: trace every requirement to its tests, and fill the gaps.
6. **08**: the qualification report on every release.
7. **06**: raw `run:` results carried intact. A product feature for users'
   A2, outside the qualification; any time after 01.

## Constraints

- The same repo rules as every feature (`CLAUDE.md`): no Python, one branch, one
  commit and one PR per closed ticket, never push to `main`. Ticket 08's release
  run is a human step (agents do not cut releases or push tags).
- The verifying check shares nothing with apply except the target.
- A check that cannot give a verdict is a **failure**, never a pass.
- A requirement that cannot name a test is rewritten until it can.

## Tickets

- [01: accept ADR-0015](issues/01-accept-adr.md)
- [02: tool requirements and the tool safety manual](issues/02-requirements-and-safety-manual.md)
- [03: the verifying check shares nothing with apply but the target](issues/03-verifying-check-fresh-state.md)
- [04: check's verdict is a boolean, and nothing skips the verifying check](issues/04-check-verdict-strict.md)
- [05: every plugin's check is proven honest by a no-op-apply test](issues/05-no-op-apply-test.md)
- [06: raw test results carried unchanged, with their hash](issues/06-raw-results-evidence.md)
- [07: the validation suite: every requirement traced to its tests](issues/07-validation-suite.md)
- [08: a qualification report for every release](issues/08-qualification-report.md)
