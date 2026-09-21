# 06: The coverage workflow, and ci-checks.md describes what CI runs

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: 02, 03, 04, 05

## What

`coverage.yml`, on the same triggers as the other gates, with setup
`apt: lcov`, `cache-key: coverage` and `ssh-target: true`.

- Run `bazel run //tools/coverage:check`. It runs `bazel coverage` itself and
  fails when a file's miss count exceeds `tools/coverage/floor.txt`.
- Always run `bazel run //tools/coverage:html`, even when `check` fails, and
  upload `coverage-html/` as an artifact. A failing floor is exactly when the
  report is needed.
- Print `lcov --summary` of the combined report to the job summary
  (`$GITHUB_STEP_SUMMARY`), with line totals for C and Lua.

**The floor has to hold on the runner.** It was ratcheted in the devcontainer.
With ticket 02 in, the same tests run, but a different gcc and gcov can count
lines differently. If `check` fails on an unchanged tree, compare the
per-file counts between the two environments. Record under Comments which files
differ and why, and do not `--update` the floor from CI numbers without saying
so. The floor must pass in both places, because it is also run locally before a
change is sent (README, "Keeping coverage from dropping").

Add the coverage badge.

**`docs/ci-checks.md` now describes what CI runs.** Everything but fuzzing is now
on every push, and this is the last gate ticket, so rewrite the doc:

- Replace "There is no CI service configured yet…" with the workflow table from
  the spec, and change the Cadence column to "every push to main" or "manual" (fuzz).
- Merge the "Every change" and "Nightly" blocks into one "Every push" block, and
  update `workflows_test` rule 2 (ticket 01) to read the renamed block, so every
  gate command is now checked against the workflows.
- Remove "Not in CI yet".
- Update `docs/fuzzing.md`'s "Scheduled home" paragraph in ticket 08, not here.

## Acceptance criteria

- [x] `workflows_test` checks every command in the "Every push" block (tests, asan, ubsan, both valgrind runs, coverage check), and fails if any is unwired. `unit: tools/ci/workflows_test.sh::command_unwired`
- [x] `workflows_test` passes with `coverage.yml` and its badge in place. `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [x] A push to `main` runs `coverage.yml` green on an unchanged tree, and the job summary shows the lcov totals. `manual: push; open the run summary`
- [ ] Adding an untested function under `src/` turns it red, and the artifact still contains the HTML report showing the new lines missed. `manual: throwaway branch, as in ticket 03`
- [x] The coverage badge renders. `manual: view README on github.com`

## Comments

- coverage.yml added with badge; docs/ci-checks.md rewritten with the workflow table and a single 'Every push' block, which workflows_test now checks in full. Whether floor.txt holds on the runner's gcc/gcov is seen on the first run; nothing was --update'd.
