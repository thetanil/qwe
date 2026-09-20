# 03: A secret `run:` output sits in a world-readable file while the step runs

Status: ready-for-agent
Category: bug
Type: task
Blocked by: none

## What

qwe-ssh-sec II.9 says a secret step output travels on the output channel and
that its plaintext joins the redaction set before any later byte is logged. For
a `run:` step the output channel is a file: the parent picks the path, exports
it as `$QWE_OUTPUT`, and the step's own shell creates the file with `>>`. That
means the file is created by the step, under the step's umask, and nothing sets
its mode.

Checked on 2026-09-20 with a step that writes a `secret-outputs:` value:

```
[j] -rw-r--r-- 1 user user 23 … /tmp/probe2/.qwe/runs/20260920T084126Z-…/j.0.output
[j] 0022
[j] got ***
```

The plaintext of a declared-secret output is mode **0644**, inside
`.qwe/runs/<id>/`, which `mkdir_p` creates **0755**. Any local user can read the
secret for as long as the step runs. The redaction and the deletion both work —
`got ***`, and nothing is left in the run directory afterwards — so this is
purely the on-disk window, not a leak into the logs or `result.json`.

The window is the whole step, not an instant: the file is written when the step
chooses and read only when the step's leader exits. A step that writes its token
early and then works for ten minutes leaves the token readable for ten minutes.

**The fix.** The parent already owns the path, so it can own the file: create it
`0600` before the step is forked (`O_CREAT | O_EXCL`, then close), so the step's
`>>` appends to a file that is already private. Creating the run directory
`0700` as well is worth doing in the same change — it holds job logs, and
although those are redacted, there is no reason for them to be world-readable.

Two details to keep right:
- `take_output_file` deletes the file after reading; that behaviour must not
  change.
- A step is free to truncate with `>` instead of appending. Truncation keeps the
  existing mode, so pre-creating at 0600 survives it. A step that deletes and
  recreates the file gets the default mode again; that is out of qwe's hands and
  not worth chasing.

## Acceptance criteria

- [ ] The `$QWE_OUTPUT` file exists before the step starts and has mode 0600, checked from inside the step itself. `e2e: tests/e2e/output_file_is_private/`
- [ ] `.qwe/runs/<id>/` is created with mode 0700. `e2e: tests/e2e/output_file_is_private/`
- [ ] A step that writes with `>` rather than `>>` still ends up with a 0600 file and its outputs are still collected. `e2e: tests/e2e/output_file_is_private/`
- [ ] The file is still deleted after the step, and a secret output is still masked and still absent from `result.json`. `e2e: tests/e2e/secret_output_not_in_result/`, `tests/e2e/output_file_cleaned/` (existing, must stay green)
- [ ] Outputs from a step that never writes the file still work (no output is not an error). `e2e: tests/e2e/run_outputs/` (existing, must stay green)
- [ ] qwe-ssh-sec II.9 records that the parent creates the output file 0600, so "travels on the output channel" has a stated meaning for `run:` steps.

## Comments

A remote `run:` step has no `$QWE_OUTPUT` at all (m1-engine ticket 13): the file
would be on the wrong host. So this ticket is about local steps and `on: local`
steps only. When remote outputs arrive, they will need their own answer to the
same question, and it should not be "a file in /tmp on the target".
