# 08: assert plugin, and smoke_file.yml (ensure, read, assert)

Status: resolved
Category: enhancement
Type: task
Blocked by: 07

## What

A built-in step plugin that lets a workflow check its own results:

- `with: actual` (string, required), exactly one of `equals` / `contains` / `matches` (a Lua pattern),
  and an optional `message`.
- It runs **no** command on the target. `check` does the comparison. On a match it reports nothing to do
  (the step is `unchanged`). On a mismatch it raises a plugin error:
  `assert: <message or "values differ">: expected <op> <expected>, actual <actual>`, with long values cut
  at 200 characters. Redaction applies as usual, so a secret compared with `assert` shows as `***`.
- The schema's `oneOf` refuses zero or more than one operator at `qwe validate`.

Add `tests/smoke/smoke_file.yml`: `file.ensure` writes a file, `file.read` reads it back, and `assert`
checks `exists`, `content`, `mode` and `sha256` (against a known hash). Then it runs `file.ensure` again
(unchanged), `file.read` + `assert` again. Add `neg_assert_mismatch.yml` to smoke.yml, with the expected
message grep.

Convert `smoke_run.yml`'s `test` checks to `assert` steps where one fits.

## Acceptance criteria

- [x] equals/contains/matches pass, and each one fails with the message form above: `e2e: tests/e2e/assert_ops/`, `e2e: tests/e2e/assert_mismatch/`
- [x] Zero or two operators are refused by `qwe validate`, with the schema path: `e2e: tests/e2e/validate_assert_one_operator/`
- [x] It runs no backend command: `plugin: plugins/builtin/assert/test.lua::no_commands` (recording backend records nothing)
- [x] An asserted secret is redacted in the error: `e2e: tests/e2e/assert_secret_redacted/`
- [x] `smoke_file.yml` passes in the Bazel suite: `e2e: tests/smoke:smoke_workflows_test`
- [x] The smoke.yml negative for a mismatch passes: `manual: push; check the run and its summary` — done, [run 35865874382, job 107198012432](https://github.com/thetanil/qwe/actions/runs/35865874382/job/107198012432): `neg_assert_mismatch` and its assertion step both `success`
- [x] `bazel test //...` green; the coverage floor holds

## Comments

- `plugins/builtin/assert/plugin.lua`: `check(with)` picks the one operator the schema's
  `oneOf` guaranteed is present (`equals` exact match, `contains` a plain substring search
  via `actual:find(expected, 1, true)` — not a pattern, `matches` a Lua pattern via
  `actual:match(expected)`), returns `false` (never a change) on a match, and raises
  `"assert: " .. (with.message or "values differ") .. ": expected " .. op .. " " .. expected
  .. ", actual " .. actual` on a mismatch, with both `expected` and `actual` cut at 200
  characters (`s:sub(1,200) .. "..."` past that, exact 200 is not cut). `apply` is an empty
  stub, same shape as `file.read` (07): all the work happens in `check`, which never touches
  `ctx.backend`.
- The `oneOf` refusal message is whatever lua-schema's own `oneOf` keyword produces — not
  something this ticket writes. Checked by hand: zero operators lists all three missing
  `required` branches plus `must match exactly one schema in oneOf`; two operators (e.g.
  `equals` + `contains`) lists only the third (`matches`) branch's `required` failure plus
  the same `oneOf` line, because `required: [equals]` and `required: [contains]` both
  independently pass when both keys are present — `oneOf` catches the *two matching
  branches*, not the extra key. `tests/e2e/validate_assert_one_operator/` covers both shapes
  in one `qwe validate` (two steps, one with zero operators and one with two).
- Redaction needed no plugin-side code: it is the kernel's existing secret-substitution
  taint (`qwe.template`'s `substitute`, used by `07`'s file.read outputs too) — a value that
  came from `${{ steps.<id>.outputs.<k> }}` on an output declared `secret-outputs:` is
  already tainted before it reaches `with.actual`, so it prints as `***` wherever it lands,
  including inside a raised plugin error. `tests/e2e/assert_secret_redacted/` reuses
  `secret_output_redacted`'s fixture key file to prove it end to end with a real mismatch.
- `plugins/builtin/assert/test.lua`: `equals_ops`, `contains_ops` (including a `%d`-bearing
  needle, to prove `contains` is plain substring search and not a pattern), `matches_ops`,
  `custom_message`, `long_values_cut_at_200` (below/at/above the 200-char boundary, on both
  `expected` and `actual`), and `no_commands`.
- `tests/e2e/assert_ops/`: three passing steps (one per operator) followed by a `run:` step,
  so a golden failure would show which operator broke. `tests/e2e/assert_mismatch/`: the
  primary golden covers the `equals` mismatch (qwe's own stdout/exit); `check.sh` runs two
  more workflows (`contains.yaml`, `matches.yaml`) through `$QWE_BIN` directly to cover the
  other two operators' message shape without three separate e2e directories — the harness
  already renames `.qwe/runs/` to `RUN/` before `check.sh` runs, so the extra invocations
  cannot disturb the primary golden.
- `tests/smoke/smoke_file.yml`: `file.ensure` writes `smoke-file.txt`, `file.read` reads it
  back, four `assert` steps check `exists`/`content`/`mode`/`sha256` (the sha256 is a fixed,
  precomputed hash of the fixed content — `printf 'hello from qwe smoke\n' | sha256sum`),
  then `file.ensure` and `file.read` run again (both `unchanged` — checked by hand against
  `--summary`) and two more `assert` steps confirm the content and mode survived. Runs for
  real in `bazel test //tests/smoke:smoke_workflows_test` (no `# smoke: skip-in-bazel`
  marker needed: `target: local`, no root, no sleeps).
- `smoke_run.yml`'s `job-env-wins-over-workflow`, `step-env-wins-over-job` and `consume`
  steps became `assert` steps using `${{ env.LEVEL }}` and `${{ steps.produce.outputs.greeting
  }}` in `with.actual`, in place of `run: test "$LEVEL" = "..."`. `${{ env.X }}` (as opposed
  to `steps.*.outputs.*`) was not something 07 needed to check, so this is the first place a
  built-in plugin's `with:` reads it; confirmed by hand that a step's own `env:` override
  (the `step-env-wins-over-job` case) is visible to its own `${{ env.LEVEL }}` before the
  plugin runs, matching the existing `run:`-based test's intent exactly, just with a message
  on failure instead of a bare `test` exit 1.
- `tests/smoke/neg_assert_mismatch.yml` + the matching `neg_assert_mismatch` /
  `neg_assert_mismatch must fail with a helpful message` step pair added to **both**
  `debug-smoke` and `smoke` jobs in `.github/workflows/smoke.yml`, right after 07's
  `neg_file_read_denied` pair, same `continue-on-error` + tee + `grep -F` shape.
- Two e2e goldens regenerated again for the growing built-in list (`file.ensure, file.read`
  -> `file.ensure, file.read, assert`): `tests/e2e/plugin_top_level_refused/expected/stderr`,
  `tests/e2e/validate_unknown_plugin/expected/stderr`. `tools/ci:workflows_test` (the drift
  test, `repo_is_consistent` in particular) still passes after the `smoke.yml` edits.
- **The GitHub-only manual criterion is not checked off**, for the same reason as 06 and 07:
  this session does not push. What was verified instead: `qwe validate` and `qwe run` against
  `neg_assert_mismatch.yml` by hand with the fastbuild binary, exit 1 and exactly `assert:
  values differ: expected equals what we wanted, actual not what we wanted` on stdout — what
  the assertion step's `grep -F` looks for.
- `bazel run //tools/coverage:check -- --update`: `assert/plugin.lua` fully covered (0
  uncovered lines), no regressions elsewhere; `floor.txt` gained one line.
- `bazel test //...`: 239 passed, 3 skipped (pre-existing: asan/ubsan/valgrind smoke), 0
  failed.

- **Update, after the user pushed (2026-09-23):** the manual criterion is closed for real —
  [run 35865874382, job 107198012432](https://github.com/thetanil/qwe/actions/runs/35865874382/job/107198012432)
  shows `neg_assert_mismatch` and its assertion step both `success`.
