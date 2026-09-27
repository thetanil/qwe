# qwe is qualified as an ISO 26262 tool for TCL3 at ASIL D, by validation

**Proposed (draft, 2026-09-26).** Accepted when
`.scratch/iso26262-tool-qualification/issues/01-accept-adr.md` is resolved; until
then nothing below is a commitment.

qwe is to be certified as a **general-purpose workflow tool**, used on work for
automotive items up to **ASIL D**. It runs on an operator host (ADR-0002) against
targets. In the reference use cases these are **devices under test (DuT)**: small
ARM boards (Raspberry Pi class) running the software being verified. Nothing of qwe
runs in the vehicle. So under ISO 26262 qwe is a **software tool**, governed by
ISO 26262-8 clause 11 ("Confidence in the use of software tools"), and not an element
of the item. Part 6's coding guidelines are not directly required of its source.

Clause 11 classifies a tool by **tool impact** (TI0: a malfunction cannot introduce
or mask an error in the item; TI1: it can) and **tool error detection** (TD1 high to
TD3 low confidence that a malfunction is prevented or detected). Together they give a
**tool confidence level**. TCL1 needs no qualification. TCL2 and TCL3 need
qualification by: 1a increased confidence from use, 1b evaluation of the tool
development process, 1c validation of the software tool, or 1d development in
accordance with a safety standard. ISO 26262-8 Tables 4 and 5 rate these per ASIL.
At ASIL D, 1c and 1d are the highly recommended ones for both TCL2 and TCL3
(confirm the cell ratings against the edition the assessor uses).

## Scope

The reference use cases are control, configuration, flashing, remote test
execution and reporting. Every one is TI1: each can make a verification step test
the wrong thing, or report a pass it did not earn. qwe is general-purpose, and what
it does is run steps, so its scope is drawn by **kind of step**:

| Step kind | Reference use cases | In the safety-rated scope? |
|---|---|---|
| **Plugin step with a built-in plugin** (`uses:`, `check` and `apply`) | control, configuration, flashing, anything a built-in plugin does | **Yes** |
| **Reporting of plugin-step outcomes** (the lifecycle outcome, `result.json`) | reporting | **Yes** |
| **Plugin step with a project plugin** | anything a user's plugin does | **No.** The user takes responsibility |
| **`run:` step** (an arbitrary command; no check exists) | remote test execution | **No.** The user takes responsibility |
| **Raw output of `run:` steps** (what the user's test framework wrote) | reporting | **No.** It is the user's record; qwe only carries it |

## Decision

**The safety-rated scope (the kernel plus the built-in plugins) is qualified for
TCL3 at ASIL D, by validation (1c).** qwe does not claim a lower TCL from its own
error detection. The certificate then holds whatever the user's context is. A user
does not need a TD argument of their own for the rated scope. They need only stay
inside it, as the tool safety manual defines it.

### Why not claim less

check → apply → check (below) catches the most common plugin-step malfunction: an
apply that did not take effect. It does not make detection **independent** of the
malfunction, because the verifying check shares a lot with apply:

- **the same author**: a misunderstanding in the plugin (for example, reading the
  image identity from the wrong place) is in both halves, which then agree;
- **the same channel**: both go through `ctx.backend`, so a backend that returns
  stale or wrong output misleads both;
- **the same target resolution**: if qwe resolves the wrong DuT, apply and check
  both reach the wrong board, converge, and report success;
- **nothing after the verdict**: the path from the verdict to the recorded outcome
  is qwe code that the check does not see.

That supports TD2 at best. At ASIL D, TD2 is TCL2, which needs qualification
anyway, by the same highly recommended method. So qualifying for TCL3 costs about
what TCL2 would, and gives a certificate that does not depend on a TD argument.

### check → apply → check is a tool requirement, and the core of what is validated

The kernel runs check, runs apply only if a change is needed, then runs check
again. A second check that still needs a change fails the step `not-converged`
(design §12.3, `qwe.checkapply`). It is built into every plugin step and cannot be
switched off. The validation proves it, and a user can still cite it in their own
safety case. These are **tool requirements** (numbered in
`docs/tool-requirements.md`):

- **The kernel:**
  - The verifying check shares nothing with apply except the target. It gets a
    freshly loaded plugin module, a fresh copy of `with`, and a fresh `ctx`, so no
    state apply leaves in memory (a module upvalue, a field written into `with`,
    something cached on the backend object) can make it pass. Today all three are
    shared (`src/kernel/lua/plugins.lua`, `run_step`). This is a known malfunction
    until fixed.
  - check's verdict must be an explicit boolean. Anything else (`nil`, a number, a
    string, a raise) is `plugin-error`, never "no change needed". Today `nil` reads
    as converged (`src/kernel/lua/checkapply.lua`). This is a known malfunction
    until fixed.
  - No option, mode or workflow key skips the verifying check or turns
    `not-converged` into success. A future `--check` mode runs only check, and
    reports a needed change as a needed change.
- **Every plugin's check.** This is a rule of plugin development, for built-in and
  project plugins alike, although only built-in plugins are safety-rated:
  - It observes the target's actual state through `ctx.backend`, and never reads
    anything apply produced: not apply's return, not a file apply wrote only as a
    record, not a cache. For the reference use cases: after a flash, read the
    booted image identity from the DuT; after a power action, read the DuT's
    `boot_id`; after configuration, read the values back.
  - It changes nothing.
  - It passes the **no-op-apply test**: with apply replaced by a no-op, on a target
    that needs a change, the step fails `not-converged`. For built-in plugins,
    `bazel test //...` enforces this. For project plugins, qwe ships the same
    harness, and the plugin-authoring docs make passing it part of developing a
    plugin.

### What validation (1c) consists of

- **Tool requirements** for the rated scope: the kernel's behaviour (lifecycle,
  outcomes and reasons, check → apply → check, target resolution, timeouts,
  result reporting) and each built-in plugin's contract.
- **A validation suite**: every requirement traced to the tests that prove it
  (unit, plugin, e2e, and the OOM, fuzz, sanitizer and valgrind runs for behaviour
  under anomalous conditions). A requirement with no test fails a check.
- **Known malfunctions**, with workarounds: open bugs that can affect an outcome,
  GCC and LuaJIT limitations, the vendored components' anomalies.
- **A qualification report per release**: the qwe version and the hash of the
  validated binary, the tool versions that built it, the requirement-to-test
  results, and the known-malfunction list. It is produced by CI and attached to
  the release.
- **The tool safety manual**: scope, use, the requirements a user relies on, what
  is outside the claim, and the known malfunctions.

### Outside the safety-rated scope: the user's responsibility

The safety manual lists these as outside qwe's claim, with what a user must have
in place to take responsibility for them:

- **`run:` steps.** qwe runs the command and reports its exit status and output.
  It makes no claim that a pass is real. The current user's arrangement is the
  example the manual gives:
  - **A1** (confirmed by the user, 2026-09-26): an independent downstream gate
    re-verifies the same requirements that the qwe-run tests verify, before the
    item is released. Its tests, tooling and execution environment may differ.
    The differences help, because a fault in qwe's path, or in the lab set-up
    around it, is unlikely to recur the same way in the gate.
  - **A2** (confirmed by the user, 2026-09-26): release and verification decisions
    rest on the raw results, which go into the user's long-term archive. qwe's
    summaries are not the determinative record.
- **Project plugins.** They run through the same check → apply → check, but qwe
  cannot vouch for a check it did not write. A user who relies on one for safety
  work qualifies it themselves.

## Rejected

- **Claiming TCL1 through the verifying check.** Detection that shares its author,
  channel and target resolution with the operation it checks is not independent
  (see "Why not claim less"). A certifier would challenge it, and the certificate
  would hinge on that argument.
- **Claiming TCL2 and qualifying for TCL2.** It is an honest classification for
  plugin steps, but at ASIL D the qualification method is the same as for TCL3, and
  the certificate would be weaker for the same effort.
- **Per-use-case checks as separate features** (a "verify flash" step, a "verify
  power" step). They would duplicate check → apply → check, could be left out of a
  workflow, and would cover only the use cases someone thought of.
- **Safety-rating `run:` steps.** A `run:` step executes an arbitrary command with
  no check. Validating it would mean validating every command a user might run.
  The user, who knows what the command means, owns it.
- **Safety-rating project plugins that pass the harness.** Passing the no-op-apply
  test shows the check looks at the target, not that it looks at the *right*
  thing. That needs the review and validation a built-in plugin gets here.
- **Development in accordance with a safety standard (1d)**, e.g. ISO 26262-6 with
  MISRA C. It needs a commercial checker, and architectural deviations (dynamic
  memory, stdio, `setjmp`/`longjmp` inside the Lua runtime, `fork`/`exec`/signals)
  that amount to a redesign. 1c is highly recommended at ASIL D on its own.
- **Calling `run:` steps a "safety element out of context".** That is a formal
  ISO 26262 term (Part 10) for an element of an *item* developed without a specific
  vehicle context, and a certifier would read it that way. The manual says instead
  that `run:` steps are outside the tool's safety claim.

## Consequences

- **The certificate covers a version, not the project.** Each release is validated
  and gets its qualification report. A change to the rated scope (kernel or a
  built-in plugin) is released only with the validation suite green and the
  report regenerated.
- **The verifying check and the outcome path are safety-relevant code.**
  `qwe.checkapply`, `plugins.lua`'s `run_step`, target resolution, the result pipe,
  and the lifecycle outcome cells are what the most important requirements rest on.
  A change to them is reviewed as such.
- **Code quality supports validation but does not gate it.** `.scratch/sca-round3`
  goes ahead on its own because its findings are real defects. Its results
  (analyzer gates, sanitizers, fuzzing) are cited in the qualification report as
  supporting evidence. ISO 26262-6 and MISRA are not adopted, and no MISRA checker
  is bought.
- **The tools that build qwe** are covered through the validated binary: the
  report names their pinned versions (`.scratch/sca-round3`, ticket 04), so the
  validated build can be reproduced.
- **Vendored code** is part of the tool and is validated with it. Its known
  anomalies go into the known-malfunction list, from the third-party register
  (`.scratch/sca-round3`, ticket 27).
