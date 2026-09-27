# 01: Accept ADR-0015

Status: ready-for-human
Category: enhancement
Type: decision

## What

Decided (2026-09-26, the user; first raised as `.scratch/sca-round3` ticket 01):

- Standard: **ISO 26262** (the edition the assessor works to is to be confirmed;
  2018 is assumed).
- qwe is certified as a **general-purpose workflow tool**, a software tool under
  ISO 26262-8 clause 11, for items up to **ASIL D**.
- Reference use cases: control, configuration, flashing, remote test execution
  and reporting, on DuTs (small ARM boards, Raspberry Pi class).
- **Safety-rated scope: the kernel and the built-in plugins.** Project plugins
  and `run:` steps are outside it; the safety manual says the user takes
  responsibility for them.
- **Every plugin's check must pass the no-op-apply test.** This is part of
  developing any plugin, built-in or project, although only built-ins are rated.
- **Qualify for TCL3 at ASIL D, by validation (1c).** check → apply → check is
  a tool requirement and the core of what is validated, but not a reason to
  claim a lower TCL: it shares its author, channel and target resolution with
  apply.
- A1 and A2 (the independent downstream gate, and the raw results in the
  long-term archive) are confirmed for the current user. They are now the
  manual's example of how a user takes responsibility for `run:` steps, not
  part of qwe's own claim.

`docs/adr/0015-qwe-is-qualified-for-tcl3-asil-d-by-validation.md` is drafted from these.

## To decide

1. Is the ADR right?
2. Does the certifier accept 1c alone for TCL3 at ASIL D, or also want 1a or 1b?
   Ask before 07 and 08 are built, because it changes what the report contains.
3. How long qualification evidence is kept (release assets do not expire;
   `.scratch/sca-round3` ticket 04 attaches CI evidence to releases).

## Acceptance criteria

- [ ] ADR-0015 loses its "Proposed" line (accepted), or is revised and then
      accepted. `manual: docs/adr/0015-qwe-is-qualified-for-tcl3-asil-d-by-validation.md`
- [x] The rated scope, the plugin rule and the target (TCL3, ASIL D, 1c) are
      decided. `manual: this ticket, "What"`
- [ ] The certifier's view on 1c alone is recorded. `manual: Comments`

## Comments

2026-09-26: A1 confirmed: the downstream gate covers the same requirements with
an independent test, which may use different tests and a different environment.

2026-09-26: A2 confirmed: summaries are not the determinative fact, and raw
results are part of the long-term archives. The checks belong in every plugin
(check, then apply if needed, then check again, and a second check that still
needs an apply is a failure), and qwe is certified as a general-purpose tool.

2026-09-26: only built-in plugins are safety-rated, and the no-op-apply test is
part of developing any plugin. `run:` steps are in the safety manual as outside
the claim, with the user taking responsibility. On whether TCL1 is right: it is
not defensible, because the verifying check is not independent of apply. The
user chose to qualify for TCL3 at ASIL D by validation, and the ADR was
rewritten and renamed accordingly.
