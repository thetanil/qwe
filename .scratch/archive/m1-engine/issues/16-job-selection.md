# 16: `--job` selection

Status: resolved
Type: task
Blocked by: 06

## What

`qwe run --job <id>` (can be repeated) runs the chosen jobs plus everything they depend on through `needs:`. Jobs that weren't chosen don't appear in `result.json`.

## Acceptance criteria

- [x] For A → B → C and D: `--job B` runs A and B only. `e2e: tests/e2e/job_select_closure/`
- [x] `--job B --job D` runs A, B and D. `e2e: tests/e2e/job_select_multiple/`
- [x] `--job nope` exits 2 with an error naming the unknown job. `e2e: tests/e2e/job_select_unknown/`
- [x] The whole workflow is still validated when `--job` is given, including jobs that weren't selected. `e2e: tests/e2e/job_select_validates_all/`

## Comments

- `--job` is parsed in `src/cli/run/run.c` and applied by `select_jobs` in `workflow.c`, after the whole workflow has been validated and loaded: it marks the chosen jobs, adds their `needs:` transitively, and drops the rest from the job list, so they neither run nor appear in `result.json`. An unknown id is refused before a run directory is made (exit 2).
- `job_select_multiple` checks `result.json` rather than stdout, because a and d run in parallel and their line order is not fixed.
- `bazel test //...` green.
