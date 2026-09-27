# 02: Tool requirements and the tool safety manual

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

Validation (1c) validates qwe against its **tool requirements**, and the user
uses it through the **tool safety manual** (ADR-0015). Neither exists yet.

## Fix

- `docs/tool-requirements.md`: numbered requirements for the safety-rated scope,
  each with the behaviour, the failure behaviour, and a line for the tests that
  prove it (ticket 07 fills in and checks the tests):
  - `TR-KRN-*`: the kernel. The lifecycle and its outcomes and reasons
    (ADR-0010, ADR-0012), check → apply → check (tickets 03, 04), target
    resolution (a step reaches the target its job names, and no other), timeouts
    and cancellation, the result pipe, `result.json`.
  - `TR-PLG-*`: the plugin rule (check observes the target, reads nothing apply
    produced, changes nothing, and passes the no-op-apply test, ticket 05).
  - One section per built-in plugin: its contract.

  Keep each requirement testable. Anything that cannot name a test is rewritten
  until it can.
- `docs/tool-safety-manual.md`:
  - the scope table from ADR-0015: what is rated, and what the user takes
    responsibility for (`run:` steps, project plugins), with A1/A2 as the example
    of how a user does that;
  - how to stay inside the rated scope (which built-in plugins, which options);
  - the plugin rule and the no-op-apply harness, for project-plugin authors;
  - **known malfunctions** with workarounds: open bugs that can affect an outcome
    (tickets 03 and 04 until they are closed), GCC and LuaJIT limitations, and the
    vendored components' anomalies from `docs/third-party.md` once
    `.scratch/sca-round3` ticket 27 lands;
  - where the per-release qualification report is (ticket 08).

## Acceptance criteria

- [ ] `docs/tool-requirements.md` covers the kernel, the plugin rule and every
      built-in plugin, each requirement numbered. `manual: read it`
- [ ] `docs/tool-safety-manual.md` has the sections above, and its scope table
      matches ADR-0015's. `manual: read it`
- [ ] `bazel test //...` is green.

## Comments
