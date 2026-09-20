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

## After quality/09

Whole report: 85.1% of lines (5083 of 5974), 88.9% of functions. (The line count is larger than
the table above's 4588 because the unit tests added since count too: `src/` includes test-support
code such as `oom_shim.c`.) Files in `src/` with a miss, hit / found. Compare a later report to this
table; `tools/luacov/floor.txt` holds the same counts as a ratchet.

Two things changed how coverage is *measured*, not only what is tested:

- **Step children are counted.** A step child ends in `execvp` or `_exit`, which threw away its gcov
  counters, so the child side of `workflow.c` (`child_argv`), `proc.c` and `luaexec.c` showed as
  untested although every e2e case runs them. `src/kernel/gcov.h` dumps (and resets) the counters
  before those exits. It compiles to nothing outside `bazel coverage` (`.bazelrc` gives that
  configuration `-DQWE_GCOV`) and does nothing unless `QWE_LUA_COVERAGE` is set; the OOM tests unset
  it, because a dump allocates and would move the injection points. About 130 lines moved from
  "missed" to "hit" this way.
- **The OOM probes are not counted.** `qwe_oom_probe` forks and `_exit`s, so the allocation-failure
  branches that `oom_test` reaches stay uncovered here. That is why every "allocation failure" line
  below is listed as owned by 07/08 rather than tested again. (Dumping from the probe was tried and
  changed nothing.)

New tests: e2e `encrypt_ok`, `encrypt_no_key`, `encrypt_no_home`, `encrypt_stdin_unreadable`,
`encrypt_too_long`, `encrypt_key_garbage`, `encrypt_key_empty`, `encrypt_key_not_regular`,
`keygen_rejects_argument`, `keygen_no_home`, `cli_no_subcommand`, `inventory_not_a_map`,
`run_dir_blocked` (the harness gained `stdin`, see `tests/e2e/run_case.sh`); unit tests in
`keyfile_test` (another owner, keygen failing to make a directory, to create the file, to write it),
`proc_test` (pipe and pipe2 failing, child exit 126 and 127), `lua_cbor_test` (malformed input, what
cannot be encoded, buffer growth), the new `luaexec_test` (bad arguments, `wait` timing out, a spawn
that cannot make pipes or fork) and `limits_test` (a key longer than the path buffer). Dead code removed:
`encode_table`'s `idx < 0` branch in `luacbor.c`, which every caller made unreachable.

| file | hit / found | the misses |
|---|---|---|
| `cli/encrypt/encrypt.c` | 45 / 50 | 21 (allocation failure), 59-60 (`sodium_init` failing), 82-83 ("encryption failed": the seal's allocation failing). All owned by 07/08 or defensive. |
| `secrets/keyfile.c` | 91 / 95 | 106, 108-109 (`sodium_init` failing), 145 (`write` interrupted: EINTR retry). Defensive. |
| `secrets/envelope.c` | 58 / 71 | 25, 29-31, 57-58, 97-98, 105-107: `sodium_init` failing and allocation failures. The latter are exercised by `secrets/oom_test`, which the report cannot see (above). |
| `kernel/proc.c` | 56 / 60 | 64-67, the child's exit 126/127 lines after the dump: run by `child_exit_codes_for_no_argv_and_no_program`, unmeasurable. |
| `kernel/luacbor.c` | 297 / 306 | 79 and 98 ("malformed container" from `cbor_value_enter_container` / `leave_container`: tinycbor reports a truncated container from the element that fails first, so these are the second line of defence), and the allocation failures 266-267, 283-286, 374 (07). |
| `kernel/luaexec.c` | 251 / 269 | 32 and 211 and 245-247 and 337-339 and 81-91 (allocation failures, 07/08), 132-133 (the line after `execvp`, reached only when exec fails), 189-191 (`poll` failing with something other than EINTR: defensive), 213 (a `read` error other than EAGAIN/EINTR: defensive). |
| `kernel/luavm.c` | 56 / 68 | allocation and `luaL_newstate` failure (07), the built-in-module load error (the modules are compiled in and tested, so it cannot fail), `qwe.luacov` failing to load in the flush (defensive). |
| `kernel/validate.c` | 207 / 248 | as 05 said: the ancestor fallback in `locate`, `escape_token` and `last_token` (job ids and keys cannot contain `/` or `~`, and a DAG error is reported only for a clean schema), and the `internal error in the validator` and `in the inventory reader` paths (the embedded Lua modules cannot fail to load). Kept rather than deleted: they keep a pointer correct if the schema ever allows those characters. |
| `kernel/workflow.c` | 1048 / 1169 | grouped below. |
| `edge/yaml/transcode.c` | 289 / 299 | 216-217 (a tag other than `!encrypted`: `check_props` refuses it first, this is the belt to its braces), 243-244 (depth: the parser's own limit fires first), 435 ("cannot encode document" with no message set), 461, 467-468 (allocation), 479 (a stack index that cannot go negative). Defensive or 07. |
| `kernel/redact.c` | 87 / 96 | 22 (a zero-length feed), 69, 109, 120-146: allocation failures (`oom_test`, not counted). |
| `kernel/luafs.c` | 61 / 71 | 39-66, the allocation failure in `qwe.fs.list` (07/08). |
| `kernel/jobs.c` | 111 / 121 | 86-87 (allocation), 102-105 and 137-140: the loops over `unimplemented_job_keys` / `unimplemented_step_keys`, both `{NULL}` today (dead until a key is listed, as 05 found). |
| `kernel/trace.c` | 36 / 45 | 17 (`open` of the trace file failing: the run directory was just made), 39 (a closed trace), 60 (`write` failing), 63-68 (`abort_hook`, called only when the lifecycle hits an impossible event), 118-119 (an errno with no name). |
| `kernel/lifecycle.c` | 58 / 64 | 210-212 (`qwe_lc_action_name`'s out-of-range guard), 297-301 (an impossible event aborts the process: only reachable by a bug, and it aborts). |
| `kernel/alloc.c` | 16 / 22 | 7-10, 18, 27, 36: `die()`, which prints and aborts. Covered by `alloc_test` in a child process the report cannot see. |
| `kernel/sink.c` | 51 / 53 | 17 (EINTR retry), 32 (allocation failure). |
| `kernel/{dag,ring,timer,preamble,luarun}.c` | one line each | `dag.c` 44 (the stack walk of a cycle report, whose loop step is not distinguished by the line counter), `ring.c` 11 and `preamble.c` 47 (allocation), `timer.c` 29 (a timeout of zero or less: no caller passes one), `luarun.c` 19 (`qwe_lua_new` failing, 07). |
| `cli/run/run.c` | 34 / 36 | 23-24, allocation. |
| `edge/yaml/positions.c` | 72 / 75 | 87-89, the tie-break of two entries at the same line and column: no document produces one (defensive). |
| `edge/yaml/fuzz_harness.c` | 30 / 35 | 19, 28, 68, 76-77: `abort()` when the harness finds a bug. Only reached by a fuzz crash. |
| `kernel/oom_shim.c`, `kernel/gcov.h` | 75 / 104, 3 / 5 | test support: the shim's forked child (above), and the lines of `qwe_gcov_dump` after the dump. |

`workflow.c`, 121 lines, by what the code does:

| group | lines | why it is not covered |
|---|---|---|
| result pipe and encoding failing (`send_result`, `send_status`, the engine-error reply) | 66-67, 73, 75, 91-92, 134-135, 143, 154, 158, 166-167, 177-178, 183-191 | a full or closed result pipe, a `memfd_create` or `write` failure, the plugin's `require` failing: needs a broken kernel or a broken embedded module. The child's exit lines (139, 189) end the process after the dump. |
| plugin returned no argv (142-143) | 142-143 | `qwe.plugins.run_step` returns one of argv or a result table; every plugin shape that returned neither is refused by the shape check (`plugin_shape_and_array_args`) before a step runs. |
| reading and creating files (`read_file`, `mkdir_p`, output file, run directory) | 201, 206-211, 232-233, 241, 245, 257, 260, 1726-1744, 1814 | allocation and OOM (07/08), and `mkdir`, `open`, `fclose` failing on a directory qwe just made. The run directory not being creatable is covered (`run_dir_blocked`). |
| ring overflow alarm, output flush (278-279, 295, 312, 330) | | EINTR retries on `write`/`read`, and the overflow alarm, which needs more output than the ring holds while the sink is stalled. |
| result buffer limits (336-348) | | a step result over `RESULT_MAX`, or the allocation for it failing. |
| runtime start-up (455-457, 504-505) | | 455-457 is covered (`inventory_not_a_map`); 504-505 is `qwe_lua_new` failing (07). |
| resource setup per job and per step (642-666, 690, 900, 938-946) | | the ring, sink, timer, output file and `asprintf` failing: file-descriptor and allocation exhaustion. The loop exit of each is one shared `fail` path, which `engine_error_spawn` covers for the fork. |
| Lua calls that cannot fail (695, 704, 739, 761-770, 785-804, 831-845) | | `pcall` of embedded modules (`qwe.plugins`, `qwe.template`, `qwe.checkapply`): they are compiled in and tested, so the `goto fail` is defensive. |
| lifecycle edges (1078-1079, 1111-1113, 1187, 1253, 1319-1332) | | the "stale" event (a message from a step that has since been replaced) needs two steps racing, cancel and timeout polled at exactly one tick, a malformed result. Timing-dependent; each needs a race or a clock to land on one tick, and has no deterministic test. |
| target and plan errors (1472-1476, 1572-1576, 1614, 1671-1672, 1712-1713) | | the dispatch-time errors after `validate` has passed (an unresolvable target, a DAG with unresolved needs, `--job` allocation): validation refuses each first, so they are the second line of defence. |
| shutdown (1763, 1777, 1786) | | `PR_SET_CHILD_SUBREAPER` failing, `realpath` failing, `run_all` returning an error: none can be made to happen without a broken kernel. |

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
