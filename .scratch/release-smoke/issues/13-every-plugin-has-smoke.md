# 13: Every built-in plugin has a smoke workflow

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 07, 08, 09, 10, 11, 12

## What

A test reads the built-in plugin registry (the embedded modules table: every `plugin.<name>` and every
`backend.<name>`). It fails if one has no smoke workflow that uses it. For a step plugin that means
`uses: <name>`; `run` counts through `run:`; `local` counts through `target: local`. `backend.ssh`
(no ssh in smoke) and `backend.recording` (test-only) are exempt, and the exemption list is in the test
with the reason for each.

Also check that every smoke workflow and negative in `tests/smoke/` is run by a step in smoke.yml, so a
new file cannot silently sit unused.

Document the smoke workflows in `docs/smoke.md`:
- the layout and naming (`smoke_<area>.yml`, `neg_<case>.yml`);
- how to run one locally;
- the negative-case pattern;
- the checklist item, added to README "Writing a plugin" for built-ins: "add a smoke workflow".

## Acceptance criteria

- [ ] A plugin in the registry without a smoke workflow fails the test (fixture case), and the repo as committed passes: `unit: tests/smoke/coverage_test.sh::missing_plugin`, `::repo_is_consistent`
- [ ] A smoke file not run by smoke.yml fails the test: `unit: tests/smoke/coverage_test.sh::unwired_file`
- [ ] `docs/smoke.md` exists and is linked from README "Where to read next": `manual: read it`
- [ ] `bazel test //...` green

## Comments
