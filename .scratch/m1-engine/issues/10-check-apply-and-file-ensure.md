# 10: Check/apply protocol, recording backend, `file.ensure`

Status: ready-for-agent
Type: task
Blocked by: 06, 09

## What

- **The check/apply sequence.** The kernel runs check, then apply only if needed, then check again. `changed` is set only if apply ran. If the second check still reports a change is needed, the step is `failed` with reason `not-converged`.
- **Fork per plugin step.** Every step plugin runs in a forked child that returns its result and outputs to the parent over a result pipe (CBOR). This result pipe is separate from stdout and stderr.
- **Recording backend.** A service plugin that records translated commands and their stdin, and returns scripted results.
- **Plugin test runner.** Runs `plugins/builtin/<name>/test.lua` under Bazel.
- **`file.ensure`.** The first generic check/apply built-in plugin (`path`, `content`, `mode`). All of its work goes through the job's backend.

## Acceptance criteria

- [ ] The first run of `file.ensure` creates the file with `changed: true`. A second, identical run gives `changed: false`. `e2e: tests/e2e/file_ensure_idempotent/`
- [ ] A mode change alone gives `changed: true` and the new mode. `plugin: plugins/builtin/file.ensure/test.lua::mode_only_change`
- [ ] A project plugin whose apply never converges fails with reason `not-converged`. `e2e: tests/e2e/not_converged/`
- [ ] A plugin that segfaults through FFI fails only its own step, with reason `plugin-error`. The next job runs normally. `e2e: tests/e2e/plugin_segfault_isolated/`
- [ ] A plugin that blocks forever is ended by the step timeout. `e2e: tests/e2e/plugin_block_timeout/`
- [ ] The recording backend returns scripted results in order and fails the test on an unexpected command. `plugin: plugins/builtin/recording/test.lua::unexpected_command_fails`
- [ ] `file.ensure` sends content on stdin, never in argv (checked through the recording backend). `plugin: plugins/builtin/file.ensure/test.lua::content_via_stdin`
- [ ] A `run:` step always reports `changed: true`. `e2e: tests/e2e/run_always_changed/`
