# 10: Check/apply protocol, recording backend, `file.ensure`

Status: resolved
Type: task
Blocked by: 06, 09

## What

- **The check/apply sequence.** The kernel runs check, then apply only if needed, then check again. `changed` is set only if apply ran. If the second check still reports a change is needed, the step is `failed` with reason `not-converged`.
- **Fork per plugin step.** Every step plugin runs in a forked child that returns its result and outputs to the parent over a result pipe (CBOR). This result pipe is separate from stdout and stderr.
- **Recording backend.** A service plugin that records translated commands and their stdin, and returns scripted results.
- **Plugin test runner.** Runs `plugins/builtin/<name>/test.lua` under Bazel.
- **`file.ensure`.** The first generic check/apply built-in plugin (`path`, `content`, `mode`). All of its work goes through the job's backend.

## Acceptance criteria

- [x] The first run of `file.ensure` creates the file with `changed: true`. A second, identical run gives `changed: false`. `e2e: tests/e2e/file_ensure_idempotent/`
- [x] A mode change alone gives `changed: true` and the new mode. `plugin: plugins/builtin/file.ensure/test.lua::mode_only_change`
- [x] A project plugin whose apply never converges fails with reason `not-converged`. `e2e: tests/e2e/not_converged/`
- [x] A plugin that segfaults through FFI fails only its own step, with reason `plugin-error`. The next job runs normally. `e2e: tests/e2e/plugin_segfault_isolated/`
- [x] A plugin that blocks forever is ended by the step timeout. `e2e: tests/e2e/plugin_block_timeout/`
- [x] The recording backend returns scripted results in order and fails the test on an unexpected command. `plugin: plugins/builtin/recording/test.lua::unexpected_command_fails`
- [x] `file.ensure` sends content on stdin, never in argv (checked through the recording backend). `plugin: plugins/builtin/file.ensure/test.lua::content_via_stdin`
- [x] A `run:` step always reports `changed: true`. `e2e: tests/e2e/run_always_changed/`

## Comments

### Resolution

- **Protocol.** A step plugin exports `check(with, ctx) -> needs_change, outputs` and `apply(with, ctx)`. `ctx.backend` is the job's backend. `src/kernel/lua/checkapply.lua` (`qwe.checkapply`) runs check, apply only if needed, then check again, and gives `not-converged` if the second check still needs a change. It is documented in design §12.3.
- **Result pipe.** `qwe_proc_spawn` now creates a second, close-on-exec pipe (`res_fd`), passed to the child function. The child sends CBOR `{ status = ok|failed|exec, reason, changed, outputs }` on it. The parent drains it in the epoll loop (`EV_RESULT`, 1 MiB cap) and reads it when the leader exits (`leader_exit_event` in `workflow.c`). A `uses:` step that dies without a message is `plugin-error`, which is what makes the segfault case fail only its own step. The plugin can only report `not-converged` or `plugin-error`.
- **Changed.** `run:` and run-like steps are `changed: true`. A check/apply step takes it from the message, and `true` if there is none (apply may have run).
- **Backends.** `qwe.exec` (`luaexec.c`) runs a command with stdin and captures stdout and stderr. `backend.local` gets `new()` and `:run(script, stdin)`. `backend.recording` (`plugins/builtin/recording/`) records commands and stdin, returns scripted results in order, and raises `unexpected command` on anything not scripted.
- **Plugin tests.** `plugins/builtin/run_plugin_test.lua` runs `<name>/test.lua` under `luarun`, with `case` and `eq`. Bazel targets are `<name>_plugin_test`. `test.lua` files are excluded from the plugin lint.
- **file.ensure.** Path and mode through `stat`, `chmod`; content through `cat > path` with the content on stdin, never argv. It outputs `mode`.
- **Fixtures changed.** The old contract ran `apply` only. `project_plugin_loads` (`hello`) now returns "needs change" until it has applied, and `strict_globals_*` now return `true` from `check`, so `apply` still runs.
- **Deviation.** Step outputs go over the pipe, but the parent ignores them until ticket 11.
- `bazel test //...` green (80 tests). The new e2e cases were run 15 times each with no flake.
