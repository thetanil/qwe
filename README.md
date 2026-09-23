# qwe

**qwe** (Qualified Workflow Engine) is a small Linux command-line tool that runs a workflow to
completion and exits. You describe the work as YAML in the style of GitHub Actions (jobs, `needs:`,
ordered steps, `env:`, `outputs`), and qwe runs it on the machine you are sitting at or, over ssh, on
other machines. It is one static binary written in C99, with no daemon and no runtime to install.

What a step can do comes from plugins written in LuaJIT. A step is either `run:` (a shell command,
which is itself a built-in plugin) or `uses: <plugin>` with `with:` inputs. The plugins ship inside
the binary, and you can add your own next to a workflow without rebuilding anything. The kernel stays
small on purpose: it owns processes, timeouts, cancellation, logs and secrets, and everything else is
a plugin. The design is written up in `docs/workflow-kernel-design.md` and the vocabulary in
`CONTEXT.md`.

It began as the engine for managing a small fleet of physical devices (power, flashing, VLAN
placement, probing) from CI, and that is still the intended use. The device plugins are not written
yet; what exists is the engine and its first plugins (`run`, `file.ensure`, `file.read`, `file.line`,
`apt.package`, `assert`, the `local` and `ssh` backends).

## A first workflow

```yaml
# w.yaml
jobs:
  prepare:
    target: local
    steps:
      - id: conf
        uses: file.ensure          # idempotent: check first, change only if needed
        with:
          path: app.conf
          content: "port = 8080\n"
          mode: "0640"
      - run: cat app.conf
  report:
    target: local
    needs: [prepare]               # runs after prepare succeeds
    steps:
      - timeout-seconds: 10
        run: echo "config is in place"
```

```
$ qwe validate w.yaml       # checks the workflow against its schemas; runs none of its code
$ qwe run w.yaml
[prepare] port = 8080
[report] config is in place
```

To run a job on another machine, name it in an inventory (`-i inventory.yaml`, or the default
location) and point the job at it with `target:`:

```yaml
targets:
  bench:
    backend: ssh
    host: 192.0.2.10
```

## What it does

- **A graph of jobs.** `needs:` builds a DAG; independent jobs run in parallel under `max-parallel`,
  and `--job <id>` runs one job with what it depends on. A failed dependency skips what follows it.
- **A process per step.** Each step runs in its own forked process group with a timeout
  (`timeout-seconds`, and a job timeout), so a hung or crashing plugin cannot take the engine down, and
  an operator cancel or timeout kills the whole group.
- **Idempotent plugins.** A plugin's `check` says whether there is work to do and `apply` does it, so a
  second run changes nothing.
- **ssh targets.** One multiplexed connection per host, `become:` for privilege, and a job whose host
  cannot be reached fails on its own without stalling the rest.
- **Secrets.** `qwe keygen` makes a key and `qwe encrypt` turns a value into an `!encrypted` blob that is
  safe to commit. Secrets reach a step only through `${{ secrets.NAME }}` in `env:` or `with:`, never in
  a command line, and are redacted from the logs.
- **Step outputs.** A step writes `name=value` to `$QWE_OUTPUT`; later steps in the job read
  `${{ steps.<id>.outputs.<name> }}`.

```
qwe run <workflow.yaml> [-i <inventory.yaml>] [--job <id>]... [--summary <file>]
qwe validate <workflow.yaml> [-i <inventory.yaml>]
qwe keygen | qwe encrypt | qwe --version
```

`qwe serve` is a stub: it prints `qwe serve: not implemented` and exits 2.

## Getting it

qwe is Linux x86_64 only. Each [release](https://github.com/thetanil/qwe/releases) attaches `qwe`,
`qwe-debug` (the same build with symbols) and `SHA256SUMS`. To build it yourself you need Bazel 8.7.0
(the version is pinned in `.bazelversion`); every dependency is vendored in the repository.

```
bazel build //src/cli:qwe //src/cli:qwe-debug              # bazel-bin/src/cli/
bazel build --config=release //src/cli:qwe //src/cli:qwe-debug  # the same codegen, symbols kept for qwe-debug: what ships
bazel test //...                                            # the unit, plugin and e2e suites
```

## Where to read next

| To learn | Read |
|---|---|
| the words qwe uses (job, step, target, outcome, reason) | `CONTEXT.md` |
| how it is built and why | `docs/workflow-kernel-design.md`, `docs/adr/` |
| the ssh backend and its security decisions | `docs/qwe-ssh-sec.md` |
| how it is checked (sanitizers, valgrind, coverage, fuzzing) | `docs/ci-checks.md` and the pages it links |
| how to write a plugin | the next section of this file |
| the smoke workflows: layout, running one locally, the negative-case pattern | `docs/smoke.md` |

## Status

[![tests](https://github.com/thetanil/qwe/actions/workflows/tests.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/tests.yml)
[![asan](https://github.com/thetanil/qwe/actions/workflows/asan.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/asan.yml)
[![ubsan](https://github.com/thetanil/qwe/actions/workflows/ubsan.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/ubsan.yml)
[![valgrind](https://github.com/thetanil/qwe/actions/workflows/valgrind.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/valgrind.yml)
[![coverage](https://github.com/thetanil/qwe/actions/workflows/coverage.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/coverage.yml)
[![coverage percent](https://img.shields.io/endpoint?url=https://thetanil.com/qwe/coverage.json)](https://thetanil.com/qwe/)
[![smoke](https://github.com/thetanil/qwe/actions/workflows/smoke.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/smoke.yml)
[![nightly](https://github.com/thetanil/qwe/actions/workflows/nightly.yml/badge.svg?branch=main)](https://github.com/thetanil/qwe/actions/workflows/nightly.yml)
[![fuzz](https://github.com/thetanil/qwe/actions/workflows/fuzz.yml/badge.svg)](https://github.com/thetanil/qwe/actions/workflows/fuzz.yml)
[![release](https://github.com/thetanil/qwe/actions/workflows/release.yml/badge.svg)](https://github.com/thetanil/qwe/actions/workflows/release.yml)

Implemented with Claude using [Matt Pocock's skills](https://github.com/mattpocock/skills).

## Writing a plugin

A project plugin lives next to the workflow, in `.qwe/plugins/<name>/`, and a step
names it with `uses: <name>`:

```
.qwe/plugins/my.plugin/
  schema.json   { "with": <JSON schema of the step's with:>, "outputs": { name: <schema + "secret": bool> } }
  plugin.lua    the code
```

`schema.json` is checked against qwe's strict metaschema. `outputs` is optional, and
every output must say `"secret": true` or `"secret": false`.

### What `plugin.lua` looks like

```lua
local M = {}

local function quote(s) return "'" .. s:gsub("'", "'\\''") .. "'" end

-- Returns true if apply has work to do (and, optionally, a table of state).
function M.check(with, ctx)
  local r = ctx.backend:run("test -f " .. quote(with.path))
  return r.code ~= 0
end

function M.apply(with, ctx)
  ctx.backend:run("touch " .. quote(with.path))
end

return M
```

A run-like plugin exports `M.argv(with, ctx)` instead of `check` and `apply`, and returns
the command to execute (see the built-in `run` plugin). Commands go through
`ctx.backend` (the job's target); a plugin's Lua itself runs on the operator host, in a
forked child, once per step. An error raised in `check` or `apply` fails the step with
reason `plugin-error`.

### What `plugin.lua` may not do

**A plugin file has no top-level code.** Everything outside a function runs when the file
is loaded, so qwe refuses any of it at `qwe validate` and `qwe run`, from the source,
without running the plugin. The top level may contain only:

- `local` declarations of constants, function literals and table constructors (of the same),
  and reads of names, fields and operators on them;
- `M.name = value` assignments (which is what `function M.name() ... end` is);
- `require("...")` of a module qwe itself provides (for example `qwe.*`);
- one final `return` of the module table.

Refused, with `file:line:col`: a call, a method call, a loop, `if`, `do`, reassigning a local,
`require` of any other file. Work, including setup, belongs in `check` or `apply` or in a
function they call.

`check` and `apply` must both be exported as functions (a function literal, or a local
function), or `argv` for a run-like plugin. `qwe validate` guarantees this from the source, so
a validated plugin always has an `apply`.

These rules are deliberately strict, and they are a current limit rather than a
principle. The known consequences:

- `M.apply = pick()` is refused as a call, even if it would return a function. Write
  `function M.apply(...)`.
- A plugin cannot `require` a shared file from the project; it is one file, or it uses qwe's modules.
- There is no load-time hook (an `init`); nowhere to put setup except `check` and `apply`.

If one of these blocks you, ask: they can be loosened for a specific need, and the check
is `src/kernel/lua/pluginshape.lua`.

### Trust

Plugins run with your privileges on the operator host. `qwe.strict` (which turns a misspelled
or undeclared global into an error) prevents mistakes; it is **not** a sandbox, and a plugin
still has `io`, `os.execute` and the rest of the interpreter. The workflow directory and the
inventory are trusted input: read a workflow directory you did not write before running it
(`qwe validate` runs none of its plugin code). See "Trust boundary" in `CONTEXT.md`.

### Coverage of plugin code

`bazel coverage` measures the Lua that is compiled into the qwe binary: `src/kernel/lua/` and
the built-in plugins under `plugins/builtin/`. A project plugin in `.qwe/plugins/` is read by
path at run time, not compiled in, so it is **not** measured.

To have a built-in plugin measured, add it to the repo like the others:

1. Put it in `plugins/builtin/<name>/` (`plugin.lua`, `schema.json`). The `:plugin_lua` glob in
   `plugins/builtin/BUILD` picks the `.lua` up, and `:shipped` there declares it to the coverage
   tool, so nothing to add for a file in that layout.
2. Register it in `MODULES` in `src/kernel/lua/BUILD` (`plugin.<name>` and `plugin_schema.<name>`),
   which is what compiles it into the binary.
3. A Lua file in a *new* directory needs its own `lua_instrumented` target (see
   `tools/coverage/defs.bzl`) listed in `data` of `//src/kernel:luavm`. Without it Bazel's lcov
   merger drops the file from the report without a warning.
4. Write the tests that should exercise it (a Lua test through `luarun`, or an e2e case under
   `tests/e2e/`). Only code those tests run is counted as hit.
5. Add a smoke workflow that uses it under `tests/smoke/` (see `docs/smoke.md`).
   `tests/smoke:coverage_test` fails `bazel test //...` if a built-in plugin has none.

One command runs the coverage and writes the HTML (needs `genhtml`, from the `lcov` package):

```
bazel run //tools/coverage:html            # writes coverage-html/index.html
bazel run //tools/coverage:html -- my-dir  # or into another directory
```

Before you send a change, check that coverage has not dropped:

```
bazel run //tools/coverage:check
```

(See "Keeping coverage from dropping" below.)

Your plugin's page is under `plugins/builtin/<name>/`, with each line marked hit or missed. The
same report covers the C; `docs/coverage.md` has the per-file numbers and how the Lua hook works.

## The binary

`bazel build //src/cli:qwe //src/cli:qwe-debug` produces two statically linked executables
(`bazel-bin/src/cli/`): `qwe`, stripped, which is what ships and what the e2e cases run, and
`qwe-debug`, the same build with its symbols. The sanitizer (`--config=asan`, `ubsan`) and
valgrind builds link dynamically, because a sanitizer runtime cannot be linked statically and
valgrind cannot intercept `malloc` in a static binary; `qwe` keeps its symbols there.

Add `--config=release` for the build that ships: the same codegen as the fastbuild default (not
`-c opt` -- `-O2` miscompiled LuaJIT's unwind path in a way that only showed up on the CI runner;
see `.bazelrc`), with `qwe-debug` still keeping its DWARF (`qwe` is stripped regardless).
`release.yml` and the smoke workflow's build both use it, and the full test suite runs under it too.

## Keeping coverage from dropping

```
bazel run //tools/coverage:check              # fails if a file has more uncovered lines than floor.txt allows
bazel run //tools/coverage:check -- --update  # after improving coverage: ratchet floor.txt down
```

`tools/coverage/floor.txt` lists, per file under `src/` and `plugins/` (C and Lua), the most
uncovered lines allowed. Untested new code raises a file's count and fails the check; a new file with misses
must be listed (run `--update` once it is tested). It is a `bazel run`, not a `bazel test`,
because a test cannot itself run `bazel coverage`; run it in CI. Commit `floor.txt` changes on purpose.

## CI

GitHub Actions runs the quality checks. Each check is its own workflow in `.github/workflows/`, which is
why the Status row has one badge per check. `docs/ci-checks.md` has the exact commands, costs and
what each one fails on.

| Workflow | Runs | When |
|---|---|---|
| `tests` | `bazel test //...` | every push to `main` |
| `asan` | the suite under AddressSanitizer and LeakSanitizer | every push to `main` |
| `ubsan` | the suite under UBSan | every push to `main` |
| `coverage` | the coverage floor, and the HTML report as an artifact | every push to `main` |
| `valgrind` | the unit tests and six e2e cases under valgrind (about 23 minutes) | by hand, nightly, and in a release |
| `nightly` | all five of the above, from fresh caches, and `fuzz` | 02:17 UTC, and by hand |
| `fuzz` | both YAML fuzz targets under asan and ubsan, on a persistent corpus | nightly (one hour), and by hand |
| `release` | all five again, then builds and publishes | a pushed tag `v*` |

- **Runner and setup.** `ubuntu-24.04`, Bazel from `.bazelversion`, the caches through
  `bazel-contrib/setup-bazel`. The shared steps are in `.github/actions/setup`. The ssh e2e cases run
  against an sshd the setup starts on `172.18.0.1`, and `QWE_E2E_REQUIRE_SSH=1` makes one that cannot
  connect a failure instead of a skip.
- **Caches.** A saved cache key never changes, so a cache slowly goes stale. The nightly deletes the
  `setup-bazel-*` caches and rebuilds them, and pushes to `main` restore the result.
- **Coverage report.** A green push to `main` publishes the HTML report and a line-coverage percentage badge (`coverage.json`) to GitHub Pages, from the last job of `coverage.yml`.
- **Fuzzing** runs for 3600 s in the nightly, never on a push or in a release. Start it by hand and read the result:
  ```
  gh workflow run fuzz.yml -f seconds=3600     # seconds: at most 19800; 300 is a quick trial
  gh run list --workflow=fuzz.yml --limit 3    # find the run
  gh run watch                                 # follow it
  gh run view --log-failed                     # after a failure
  gh run download -n fuzz-findings             # the crash files and per-process logs
  ```
  The job summary lists each process's executions, corpus and coverage. The corpus persists between
  runs in the Actions cache. See `docs/fuzzing.md`.
- **Drift.** `//tools/ci:workflows_test` (part of `bazel test //...`) fails if a workflow has no badge, a
  command in `docs/ci-checks.md` is in no workflow, or the nightly or release stops calling a gate.

To check a change before pushing, run the same commands locally (`docs/ci-checks.md`); the workflows run
nothing else.

### Making a release

A release is a pushed tag `v<version>`. Nothing else starts one: a plain `git push` sends commits,
never tags, so a tag that was only created locally does nothing.

1. Bump `QWE_VERSION` in `src/kernel/qwe.h` and the expected output in
   `tests/e2e/cli_version/expected/stdout` (`qwe <version>`). Run `bazel test //...`.
2. Commit and `git push` to `main`. Wait until `tests`, `asan`, `ubsan` and `coverage` are green **on
   that commit**. The release reruns every gate at the tag, so a red `main` means a failed release.
3. Tag that exact commit, then push the tag by name:
   ```
   git tag v0.2.0                # the tag is "v" + QWE_VERSION, or the version check fails
   git push origin v0.2.0        # this is the step that starts the release
   ```
   (Instead, you can write the release in the GitHub web UI with the tag `v<version>` and publish it;
   the tag it creates starts the same run.)
4. Watch `release` in the Actions tab. It runs all five gates (valgrind included, about 25 minutes),
   builds `qwe` and `qwe-debug`, checks that the tag, `QWE_VERSION` and `qwe --version` agree, then
   attaches the binaries and `SHA256SUMS`, and adds the commit hash to the notes. A tag with a `-`
   (`v0.3.0-rc1`) is marked a pre-release.
5. If a gate or the version check fails, a release you wrote in the web UI goes back to a draft. Fix
   `main`, then move the tag: `git tag -d v0.2.0 && git push origin :refs/tags/v0.2.0`, tag the fixed
   commit and push it again.

