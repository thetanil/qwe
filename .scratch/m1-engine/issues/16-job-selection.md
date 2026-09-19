# 16: `--job` selection

Status: ready-for-agent
Type: task
Blocked by: 06

## What

`qwe run --job <id>` (can be repeated) runs the chosen jobs plus everything they depend on through `needs:`. Jobs that weren't chosen don't appear in `result.json`.

## Acceptance criteria

- [ ] For A → B → C and D: `--job B` runs A and B only. `e2e: tests/e2e/job_select_closure/`
- [ ] `--job B --job D` runs A, B and D. `e2e: tests/e2e/job_select_multiple/`
- [ ] `--job nope` exits 2 with an error naming the unknown job. `e2e: tests/e2e/job_select_unknown/`
- [ ] The whole workflow is still validated when `--job` is given, including jobs that weren't selected. `e2e: tests/e2e/job_select_validates_all/`
