# 11: Env scopes, `${{ }}` templating, step outputs

Status: ready-for-agent
Type: task
Blocked by: 10, 21

## What

- **Env.** Workflow, job and step `env:` scopes, where the innermost wins, delivered through the **stdin preamble**. The `local` backend uses the same preamble as ssh, so the path is shared. The preamble must preserve the command's own stdin exactly.
- **Templating.** `${{ env.X }}` and `${{ steps.<id>.outputs.<k> }}`, evaluated in the parent just before each step starts. Unknown contexts are validation errors. (`vars` comes in ticket 12, `secrets` in ticket 15.)
- **Outputs.** Plugin outputs arrive over the result pipe. `run:` steps write to `$QWE_OUTPUT` in GitHub's format, and the backend reads the file back and deletes it. Outputs are visible only within the same job.

## Acceptance criteria

- [ ] Step env overrides job env, which overrides workflow env, for the same key. All three scopes are visible to the step. `e2e: tests/e2e/env_scopes/`
- [ ] No env value appears in any child process's argv: the test reads `/proc/<pid>/cmdline` of the running step. `e2e: tests/e2e/env_not_in_argv/`
- [ ] After the preamble, the command's stdin is byte-identical to what was sent, including binary data with NUL bytes and data that has no trailing newline. `unit: src/kernel/preamble_test.c::stdin_passthrough_exact`
- [ ] `run:` writes `who=me` and a multi-line `key<<EOF` value to `$QWE_OUTPUT`, and a later step reads both through `${{ steps.a.outputs.* }}`. `e2e: tests/e2e/run_outputs/`
- [ ] Printing `x=1` to stdout does **not** create an output. `e2e: tests/e2e/stdout_is_not_output/`
- [ ] A reference to another job's step outputs is a validation error. `e2e: tests/e2e/outputs_job_scoped/`
- [ ] A step changing its environment at run time (`export FOO=1`) isn't visible to the next step. `e2e: tests/e2e/no_runtime_env_carryover/`
- [ ] The `$QWE_OUTPUT` file is gone from the target after the step. `e2e: tests/e2e/output_file_cleaned/`
