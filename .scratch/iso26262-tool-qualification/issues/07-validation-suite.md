# 07: The validation suite: every requirement traced to its tests

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 02

## What

ADR-0015 qualifies the rated scope by validation (1c). That means evidence that
qwe meets each tool requirement (ticket 02), including under anomalous
conditions. qwe already has most of the tests: unit, plugin, e2e, and the OOM,
fuzz, sanitizer and valgrind runs. What is missing is the **trace**: which test
proves which requirement, and a check that no requirement goes unproven.

## Fix

- Tests name the requirements they prove, in one machine-readable form
  (a `-- TR-KRN-3` comment line in Lua tests, `/* TR-KRN-3 */` in C tests, a
  `requirements` file in an e2e case directory; choose one form per test kind and
  document it).
- `tools/ci/requirements_test.sh`:
  - every requirement in `docs/tool-requirements.md` is claimed by at least one
    test;
  - every claimed id exists;
  - it prints the requirement-to-test table, which ticket 08's report uses.
- For each requirement with no test today, write one. Start with the kernel's
  outcome path, target resolution, and check → apply → check (tickets 03-05's
  tests claim their ids).
- Anomalous conditions: each `TR-KRN-*` names which of the OOM, fuzz, sanitizer
  and valgrind configurations also cover it. The table says so per requirement.

## Acceptance criteria

- [ ] Every requirement is claimed by a test, and every claim names a real
      requirement. `unit: tools/ci/requirements_test.sh`
- [ ] A throwaway requirement with no test fails that check, and so does a test
      claiming a non-existent id. `unit: the check's own negative cases`
- [ ] The requirement-to-test table is produced by a command, not by hand.
      `manual: the command, in docs/tool-requirements.md`
- [ ] `bazel test //...` is green.

## Comments

2026-09-26: this replaces the earlier "fallback" ticket (which was
`.scratch/sca-round3` ticket 30 before that). ADR-0015 made validation the plan,
not the fallback. The 1d material it held (the ISO 26262-6 Table 6 and MISRA
mapping) is no longer pursued; ADR-0015 lists 1d under "Rejected".
