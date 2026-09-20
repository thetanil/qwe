# Coverage

Coverage is a diagnostic, not a target. It shows which branches no test reaches;
the useful question is whether each of them is an error path that should have a test.

```
bazel coverage //... --combined_report=lcov
lcov --summary bazel-out/_coverage/_coverage_report.dat
```

The combined report (`bazel-out/_coverage/_coverage_report.dat`, lcov format) covers the
unit tests **and** the e2e cases together. The e2e cases run the real `qwe` binary, and its
coverage is collected from the process, so they count: leaving them out would send the
work to code that is already exercised end to end. It takes about 35 s on a warm cache.

Per file, lines hit / lines found (`src/` only; vendored code is upstream's):

```
awk -F: '/^SF:/{f=$2} /^LF:/{lf[f]=$2} /^LH:/{lh[f]=$2} END{for(f in lf) if (f ~ /^src\//) printf "%-40s %d/%d\n", f, lh[f], lf[f]}' \
    bazel-out/_coverage/_coverage_report.dat | sort
```

The lines a file misses, for example `src/kernel/luafs.c`:

```
awk -v f=src/kernel/luafs.c '/^SF:/{on=($0=="SF:"f)} on&&/^DA:/{split(substr($0,4),a,","); if(a[2]==0) print a[1]}' \
    bazel-out/_coverage/_coverage_report.dat
```

Branch data is not produced by this toolchain (`lcov` reports "no data found"); lines and
functions are.

## Baseline (2026-09-20, before quality/05's tests)

Whole report: 74.2% of lines (3404 of 4587), 85.8% of functions (314 of 366), 139 test
targets plus 110 e2e cases run.

| file | lines hit / found |
|---|---|
| `src/cli/run/run.c` | 32 / 36 |
| `src/cli/validate/validate.c` | 8 / 16 |
| `src/kernel/sink.c` | 49 / 53 |
| `src/kernel/result.c` | 62 / 78 |
| `src/kernel/trace.c` | 34 / 44 |
| `src/kernel/jobs.c` | 111 / 121 |
| `src/kernel/validate.c` | 204 / 248 |
| `src/kernel/luaexec.c` | 193 / 268 |
| `src/kernel/luafs.c` | 51 / 71 |
| `src/kernel/luasecrets.c` | 39 / 45 |
| `src/cli/encrypt/encrypt.c` | 5 / 50 |
| `src/kernel/proc.c` | 34 / 59 |
| `src/kernel/workflow.c` | 979 / 1161 |

## After quality/05

Whole report: 75.5% of lines (3463 of 4588).

| file | lines hit / found |
|---|---|
| `src/cli/run/run.c` | 34 / 36 |
| `src/cli/validate/validate.c` | 16 / 16 |
| `src/kernel/sink.c` | 51 / 53 |
| `src/kernel/result.c` | 78 / 78 |
| `src/kernel/trace.c` | 36 / 45 |
| `src/kernel/jobs.c` | 111 / 121 |
| `src/kernel/validate.c` | 204 / 248 |
| `src/kernel/luaexec.c` | 206 / 268 |
| `src/kernel/luafs.c` | 61 / 71 |
| `src/kernel/luasecrets.c` | 42 / 45 |

Still open, and not in the ticket's table: `src/cli/encrypt/encrypt.c` (5/50) and
`src/kernel/proc.c` (34/59) are the two largest gaps left in `src/`.

## Lua

The same command measures the Lua qwe ships (`src/kernel/lua/*.lua`, `plugins/builtin/**`):
its lines are in `bazel-out/_coverage/_coverage_report.dat` next to the C, and the per-file awk
above works on them (`f ~ /\.lua$/`).

```
awk -F: '/^SF:/{f=$2} /^LF:/{lf[f]=$2} /^LH:/{lh[f]=$2} END{for(f in lf) if (f ~ /\.lua$/) printf "%-45s %d/%d\n", f, lh[f], lf[f]}' \
    bazel-out/_coverage/_coverage_report.dat | sort
```

How it works. `.bazelrc` sets `QWE_LUA_COVERAGE=1` for `bazel coverage` only. When that and
`COVERAGE_DIR` (set by bazel) are both in the environment, `qwe_lua_new` turns on
`qwe.luacov`, a `debug.sethook` line hook, and the state writes `luacov-<pid>.dat` (lcov) into
`COVERAGE_DIR` when it closes; bazel merges every `.dat` there. Two things make that work:

- **The step child.** `qwe run` forks a child that runs the plugin and then execs the step, and the
  exec throws its counts away. The child calls `qwe_lua_coverage_flush` first, so it writes a file
  of its own. `e2e: luacov_child_process` checks that a file per process exists and that the plugin's
  lines are in one.
- **The manifest.** Bazel's lcov merger drops a source that is not in the instrumented-files manifest.
  `//src/kernel/lua:shipped` and `//plugins/builtin:shipped` (rule `lua_instrumented`,
  `tools/luacov/defs.bzl`) declare the Lua, and `//src/kernel:luavm` carries them as `data`.
  A new shipped Lua file under a new directory needs the same.

`luacov.lua` itself is not measured (its own test replaces the hook). The vendored Lua under
`third_party/` is not reported, like the vendored C. A module no test ever `require`s shows as 0 hits,
not absent.

### Baseline (2026-09-20, before quality/10's tests)

Lua total 1127 of 1190 lines (94.7%).

| file | lines hit / found |
|---|---|
| `plugins/builtin/backend_local/local.lua` | 20 / 20 |
| `plugins/builtin/backend-ssh/ssh.lua` | 148 / 150 |
| `plugins/builtin/file.ensure/plugin.lua` | 43 / 43 |
| `plugins/builtin/recording/recording.lua` | 25 / 27 |
| `plugins/builtin/run/plugin.lua` | 5 / 5 |
| `src/kernel/lua/become.lua` | 15 / 16 |
| `src/kernel/lua/checkapply.lua` | 12 / 12 |
| `src/kernel/lua/compat53.lua` | 25 / 26 |
| `src/kernel/lua/inventory.lua` | 121 / 134 |
| `src/kernel/lua/plugincheck.lua` | 79 / 100 |
| `src/kernel/lua/pluginshape.lua` | 93 / 95 |
| `src/kernel/lua/plugins.lua` | 132 / 133 |
| `src/kernel/lua/strict.lua` | 19 / 19 |
| `src/kernel/lua/template.lua` | 203 / 216 |
| `src/kernel/lua/validate.lua` | 187 / 194 |

### After quality/10

Lua total 1180 of 1190 (99.2%). Files still short:

| file | hit / found | the misses |
|---|---|---|
| `backend-ssh/ssh.lua` | 148 / 150 | 215-216, the socket-directory setup: exercised by the `needs-ssh` cases, which skip (and pass) unless the devcontainer's ssh target is reachable. **Dismissed**, not measurable here. |
| `recording/recording.lua` | 25 / 27 | 32, 35, the failure messages of a test double when a test's expectation is wrong. **Dismissed**: they fire only on a broken test. |
| `become.lua` | 15 / 16 | 15, the runtime guard for a bad `become:`. **Dismissed**: the schema refuses every such value first (`validate_become_type`, `validate_become_value`). |
| `compat53.lua` | 25 / 26 | 15, an upstream shim's failure branch. **Dismissed**: vendored behaviour. |
| `plugincheck.lua` | 96 / 100 | 101-102 (luacheck's own `fatal`) and 116-117 (`loadstring` failing). **Dismissed**: luacheck reports a syntax error as an ordinary issue, so a plugin that will not parse is refused before either runs (`plugin_files_malformed`, `syntax`). |

Tests added, all `qwe validate` cases with the exact message and position as the golden:
`validate_secret_not_visible`, `validate_vars_at_workflow_level`, `validate_env_names`,
`validate_become_value`, `validate_unknown_target`, `validate_step_run_and_uses`,
`inventory_bad_names`, `plugin_files_malformed`, `plugin_shape_and_array_args`.
