# 11: Env scopes, `${{ }}` templating, step outputs

Status: resolved
Type: task
Blocked by: 10, 21

## What

- **Env.** Workflow, job and step `env:` scopes, where the innermost wins, delivered through the **stdin preamble**. The `local` backend uses the same preamble as ssh, so the path is shared. The preamble must preserve the command's own stdin exactly.
- **Templating.** `${{ env.X }}` and `${{ steps.<id>.outputs.<k> }}`, evaluated in the parent just before each step starts. Unknown contexts are validation errors. (`vars` comes in ticket 12, `secrets` in ticket 15.)
- **Outputs.** Plugin outputs arrive over the result pipe. `run:` steps write to `$QWE_OUTPUT` in GitHub's format, and the backend reads the file back and deletes it. Outputs are visible only within the same job.

## Acceptance criteria

- [x] Step env overrides job env, which overrides workflow env, for the same key. All three scopes are visible to the step. `e2e: tests/e2e/env_scopes/`
- [x] No env value appears in any child process's argv: the test reads `/proc/<pid>/cmdline` of the running step. `e2e: tests/e2e/env_not_in_argv/`
- [x] After the preamble, the command's stdin is byte-identical to what was sent, including binary data with NUL bytes and data that has no trailing newline. `unit: src/kernel/preamble_test.c::stdin_passthrough_exact`
- [x] `run:` writes `who=me` and a multi-line `key<<EOF` value to `$QWE_OUTPUT`, and a later step reads both through `${{ steps.a.outputs.* }}`. `e2e: tests/e2e/run_outputs/`
- [x] Printing `x=1` to stdout does **not** create an output. `e2e: tests/e2e/stdout_is_not_output/`
- [x] A reference to another job's step outputs is a validation error. `e2e: tests/e2e/outputs_job_scoped/`
- [x] A step changing its environment at run time (`export FOO=1`) isn't visible to the next step. `e2e: tests/e2e/no_runtime_env_carryover/`
- [x] The `$QWE_OUTPUT` file is gone from the target after the step. `e2e: tests/e2e/output_file_cleaned/`

## Comments

### Resolution

- **Preamble.** `src/kernel/preamble.c` builds it and holds the bootstrap script; `qwe.exec.preamble` and `qwe.exec.bootstrap` expose it to Lua. `backend.local` has `:command(script, stdin)` (argv plus stdin) used by both the `run` plugin and `:run`. The child sets a `run:` step's stdin to a memfd holding the preamble. Unit test `preamble_test` covers NULs, no trailing newline, leading blank lines, and pipe and file stdin.
- **Templating.** `src/kernel/lua/template.lua` (`qwe.template`): `check` at validation, `resolve` in `step_spawn` (`resolve_step`). Validation also rejects env names that are not identifiers, and `QWE_OUTPUT`. Unknown contexts, including `vars` and `secrets` for now, are validation errors.
- **Decisions the ticket left open.**
  - An `env:` value may not read `env` (no evaluation order needed), only `steps.*`.
  - An output that is not set reads as an empty string, as in GitHub.
  - Lines of a `$QWE_OUTPUT` file that are not `key=value` or a `key<<DELIM` block are ignored.
- **Outputs.** Plugin outputs are read from the result message; `run:` outputs from `$QWE_OUTPUT`, under the run directory, taken and deleted when the leader exits. Each job has its own table. `result.json` gets an `outputs` object for a step that has some (it is absent otherwise, so existing goldens did not change except `file_ensure_idempotent`). Secret outputs are ticket 15.
- Removed the "not implemented" refusal of `env:` in `jobs.c`. `become`, `on` and `secret-outputs` are still refused.
- Existing project plugins that return an argv now get `(with, ctx)`; `run` uses `ctx.backend:command`.
- Extra cases beyond the criteria: `plugin_outputs_flow`, `validate_unknown_context`, `validate_unknown_output_key`, and `template_test.lua`.
- `bazel test //...` green (92 tests).
