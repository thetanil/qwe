# 07: file.read plugin

Status: resolved
Category: enhancement
Type: task
Blocked by: 06

## What

A built-in step plugin that reads a file on the job's target, through the execution backend (so it
works over ssh too), and exposes it as step outputs:

- `with: path` (required).
- Outputs (none secret): `exists` (`true`/`false`), `content`, `sha256`, `mode` (4-digit octal),
  `owner` (user name).
- It changes nothing: `check` reports nothing to do, so the step is always `unchanged`. The work is
  done in `check`, which runs once.
- A missing file is **not** an error: `exists=false` and the other outputs empty. A file that exists but
  cannot be read fails with the 06 message form (copy 06's local helper): `file.read: cannot read <path>: Permission denied`.
- The content is passed back intact, trailing newline included.

Register it the way the README's "Writing a plugin" / coverage section describes: in the modules
table, its schema, and its Lua test. Add a GitHub negative `neg_file_read_denied.yml` (a `chmod 000`
file) to smoke.yml.

## Acceptance criteria

- [x] An existing file: all five outputs are right, and a later `run:` step echoes them: `e2e: tests/e2e/file_read_outputs/`
- [x] A missing file: `exists=false`, success: `e2e: tests/e2e/file_read_missing/`
- [x] An unreadable file: the one-line error, `failed`/`plugin-error`: `e2e: tests/e2e/file_read_denied/` (skipped as root)
- [x] Command shapes and parsing against the recording backend (path quoting, a stat failure, a sha256sum failure): `plugin: plugins/builtin/file.read/test.lua::outputs`, `::missing`, `::denied`
- [x] The summary shows file.read as `unchanged`: `e2e: tests/e2e/file_read_outputs/` (summary.md golden)
- [ ] The smoke.yml negative for an unreadable file passes: `manual: push; check the run` (not run, see comments)
- [x] `bazel test //...` green; the coverage floor holds (new file listed with `--update`)

## Comments

- `plugins/builtin/file.read/plugin.lua`: `check` runs `stat -c '%a %U' -- <path>` (mode and
  owner in one call), then `cat -- <path>` for content, then `sha256sum -- <path>` for the
  hash. A failed `stat` means "missing" (`exists=false`, every other output `""`) — not an
  error. A failed `cat` or `sha256sum` after a successful `stat` raises `file.read: cannot
  read <path>: <OS reason>`, reusing 06's `clean_reason` helper verbatim (copied, per 06's
  "no shared module" call) alongside a small local `octal_mode` (`"600"` -> `"0600"`).
  `check` always returns `needs = false`, so `apply` (an empty stub, required by
  `qwe.pluginshape`) is never called and the work happens exactly once, as the ticket asks.
- Registered in `plugins.lua`'s `BUILTIN` list, `src/kernel/lua/BUILD`'s `MODULES` (`plugin.file.read`,
  `plugin_schema.file.read`), and `plugins/builtin/BUILD`'s plugin-test loop, following
  `file.ensure`'s pattern exactly.
- Two e2e goldens needed regenerating because the built-in plugin list grew:
  `tests/e2e/plugin_top_level_refused/expected/stderr` and
  `tests/e2e/validate_unknown_plugin/expected/stderr` (`unknown plugin ... (built-in:
  file.ensure)` -> `... file.ensure, file.read)`).
- `tests/e2e/file_read_outputs/`: `file.ensure` writes `out.txt`, `file.read` reads it back,
  and a `run:` step (through `${{ steps.read.outputs.* }}` templating) writes the five
  outputs to `out.env`. `check.sh` compares `content`/`mode` against the fixed literals and
  `sha256`/`owner` against `sha256sum out.txt` and `id -un` run for real, so the golden does
  not hard-code a hash or a username. `--summary summary.md` is asserted too, with `file.read`
  showing `unchanged`.
- `tests/e2e/file_read_missing/`: `file.read` on a path that does not exist; `check.sh`
  confirms `exists=false` and every other output is the empty string, and the run's exit
  code is 0 (no `expected/exit`, default).
- `tests/e2e/file_read_denied/`: `setup.sh` writes `secret.txt` then `chmod 000`s it
  (`needs-non-root`, since root ignores the permission and the case would stop being
  negative). Exit 1, stdout `qwe: plugin failed: file.read: cannot read secret.txt:
  Permission denied`, `result.json` reason `plugin-error` — the same shape as
  `file_ensure_write_denied` from 06.
- `plugins/builtin/file.read/test.lua`: `outputs` (a full read, mode+owner+content+sha256,
  checked against a recording-backend script), `missing` (a failed `stat` stops after one
  call — no `cat`, no `sha256sum`), `denied` (a `cat` failure, a `sha256sum` failure, and a
  stderr with no `": "` to confirm `clean_reason`'s fallback branch), and `quotes_the_path`
  (an apostrophe in the path, mirroring `file.ensure`'s test).
- `tests/smoke/neg_file_read_denied.yml` + the matching `neg_file_read_denied` /
  `neg_file_read_denied must fail with a helpful message` step pair added to **both**
  `debug-smoke` and `smoke` jobs in `.github/workflows/smoke.yml`, copying 06's
  `neg_file_ensure_denied` pattern exactly (`continue-on-error`, tee to
  `$RUNNER_TEMP/neg_file_read_denied.out`, `grep -F` the expected message). The Bazel
  `smoke_workflows_test` skips `neg_*.yml` by design (06), so this only runs for real in
  CI; `qwe validate` against the new file was run by hand here and passed.
- **The GitHub-only manual criterion is not checked off**, for the same reason as 06: this
  session does not push (`thetanil/qwe/CLAUDE.md`'s "**Never push.**"). What was verified
  instead: `bazel-bin/src/cli/qwe validate tests/smoke/neg_file_read_denied.yml` passed, and
  `bazel-bin/src/cli/qwe run tests/smoke/neg_file_read_denied.yml` (built with the fastbuild
  binary, run by hand) exited 1 with exactly `file.read: cannot read secret.txt: Permission
  denied` on stdout, which is what the assertion step's `grep -F` looks for.
- `bazel run //tools/coverage:check -- --update`: `file.read/plugin.lua` is fully covered
  (0 uncovered lines) by the plugin test plus the e2e cases; `floor.txt` also picked up two
  incidental improvements from the added e2e coverage (`src/kernel/validate.c` 41 -> 37,
  `src/kernel/summary.h` newly listed at 0), which is the ratchet working as designed.
- `bazel test //...`: 234 passed, 3 skipped (pre-existing: asan/ubsan/valgrind smoke), 0
  failed.
