# 13: Every built-in plugin has a smoke workflow

Status: resolved
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

- [x] A plugin in the registry without a smoke workflow fails the test (fixture case), and the repo as committed passes: `unit: tests/smoke/coverage_test.sh::missing_plugin`, `::repo_is_consistent`
- [x] A smoke file not run by smoke.yml fails the test: `unit: tests/smoke/coverage_test.sh::unwired_file`
- [x] `docs/smoke.md` exists and is linked from README "Where to read next": `manual: read it`
- [x] `bazel test //...` green

## Comments

`tests/smoke/coverage_test.sh` (new, mirrors `tools/ci/workflows_test.sh`'s
fixture/case_* shape) has two checks: `check_plugin_coverage` reads `src/kernel/lua/BUILD`'s
`MODULES` table for every `plugin.<name>` / `backend.<name>` (that table *is* "the
embedded modules table" the ticket names — no separate registry exists) and greps
`tests/smoke/*.yml` + `*.yml.in` for `uses: <name>` (`run:` for `run`, `target: local`
for `backend.local`); `check_smoke_wired` requires every smoke file the generic Bazel run
*cannot itself verify* — `skip-in-bazel`, or a `neg_*` with no `expect-fail` marker (see
12's marker convention) — to be named in `smoke.yml`. A plain workflow or a
marker-carrying negative does *not* need an explicit mention: `glob(["*.yml"])` in
`tests/smoke/BUILD` already puts it in front of `smoke_workflows_test.sh`, so it cannot
go silently unused. Read literally, "every smoke workflow ... is run by a step in
smoke.yml" would fail against the current repo (`smoke_file.yml`, `smoke_graph.yml`,
`smoke_project_plugin.yml` have no dedicated named step, by 12's own precedent of
matching `smoke_file.yml`) — I took "run by a step" to include the generic
`bazel test --config=release //...` step, since that's what actually exercises them; see
`docs/smoke.md` for the fuller writeup of this distinction.

Two `data`-visibility wrinkles worth remembering: the fixture tests need
`//src/kernel/lua:BUILD` as a real Bazel label, which needed `exports_files(["BUILD"],
visibility = [...])` added there (a BUILD file is not otherwise a referenceable source
file), and `.github:ci_files` needed `//tests/smoke:__pkg__` added to its visibility list
(it only allowed `//tools/ci:__pkg__` before). Also hit the same bash-variable-shadowing
bug twice while writing the fixture cases: `rc` used as the case function's own local
result *and* as the outer case-runner loop's global pass/fail counter, with the same name
— a case that "PASS"ed still made the whole script exit 1, silently, because the
function's `rc=1` (on the *expected* failure of the broken tree) overwrote the loop's
`rc`. Fixed with `local` inside every `case_*` function; worth grepping for in future
fixture-style test scripts in this repo (`workflows_test.sh` avoids it by not naming
anything `rc` inside its own case functions).

`docs/smoke.md` added, covering layout/naming, running one locally, the negative-case
pattern (both kinds: GitHub-only continue-on-error, and the new Bazel-verifiable
`expect-fail` marker), and how `coverage_test` keeps both from rotting. Linked from
README's "Where to read next", and "Writing a plugin"'s built-in checklist grew a fifth
item: add a smoke workflow.

`bazel test //...` and `bazel run //tools/coverage:check` both green (246 tests pass, 3
sanitizer/valgrind smoke tests skipped as usual locally; no plugin.lua changes, so the
coverage floor is untouched).
