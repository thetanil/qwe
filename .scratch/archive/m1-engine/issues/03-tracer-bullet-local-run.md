# 03: Tracer bullet: `qwe run` executes one `run:` step locally

Status: resolved
Type: task
Blocked by: 01, 02

## What

The thinnest possible end-to-end path, going through every layer:
- vendored libyaml transcodes the workflow to CBOR,
- vendored LuaJIT loads the built-in `run` step plugin,
- the kernel forks the step, and the child calls the `local` backend's translation (`sh -c <script>`) and execs,
- the epoll loop drains the pipes into the job's ring, and the tee sends them to the log sink (terminal lines prefixed with the job id, plus `.qwe/runs/<run-id>/<job>.log`),
- SIGCHLD resolves the step, and `result.json` is written.

One job (with `target: local`, which is required on every job) and one step. No `needs:`, no env, no schema validation beyond "the file parses". The later tickets deepen each layer.

## Acceptance criteria

- [x] A workflow with `run: echo hello` prints `[j] hello`, writes `hello` to `j.log`, writes a `result.json` with step and job `success`, and exits 0. `e2e: tests/e2e/tracer_hello/`
- [x] `run: exit 3` gives step and job `failed` with reason `exit-code`, and qwe exits 1. `e2e: tests/e2e/tracer_exit_code/`
- [x] stdout and stderr are merged into one log in the order they arrived. `e2e: tests/e2e/tracer_stderr_merged/`
- [x] The step's child is its own process-group leader (its pgid equals its pid, and differs from qwe's pgid). `unit: src/kernel/proc_test.c::child_is_group_leader`
- [x] 10 MiB of output from a step doesn't stall the loop, and every byte reaches the log (ring drains within the 1 MiB capacity). `e2e: tests/e2e/tracer_large_output/`
- [x] The ring records drops when the reader falls behind and never overwrites without counting. `unit: src/kernel/ring_test.c::drop_accounting`
- [x] TinyCBOR `v7.0` is vendored in `third_party/tinycbor/` as a plain `cc_library` (its two CMake-generated headers written by hand), builds with strict C99, and round-trips a map, an array, a byte string and a value carrying the secret tag. `unit: third_party/tinycbor/roundtrip_test.c::tagged_value_survives` (moved from ticket 02)

## Comments

Done. `bazel test //...` passes (14 tests, also with `--nocache_test_results`).
- **Vendored** (all build under Bazel with no network at build time):
  - TinyCBOR `v7.0` and libyaml `0.2.5`, trimmed to the files the build uses.
  - LuaJIT `v2.1` at `c6ffc141a876`, x86-64 only. Its Makefile's host steps are replayed as genrules: `minilua` runs `genversion` and `dynasm`, then `buildvm` writes `lj_vm.S` and the `lj_*def.h` headers. `third_party/luajit/relver.txt` holds the commit time the Makefile would read from git.
  - Third-party targets build with `-Wno-error` (libyaml) or `-std=gnu99` (LuaJIT); our own code keeps `-std=c99 -Wall -Wextra -Werror`.
- **Built-in plugins** are embedded as Lua *source* for now (`tools/embed.lua` on minilua, into `plugins/builtin/embedded.c`). Compiling them to bytecode is ticket 09's.
- **Scope of `qwe run`:** exactly one job, `target: local`, exactly one `run:` step, no flags. Anything else exits 2 with a message. Tickets 06, 12 and 16 lift this.
- **`qwe.cbor`:** only the CBOR→Lua direction exists (`src/kernel/luabridge.c`). Lua→CBOR arrives with the result pipe.
- **Step exec:** the forked child asks the `run` plugin (Lua) for an argv via the `local` backend, then `execvp`s. SIGCHLD is blocked and read through a signalfd in the epoll loop.
- **Harness changes** (`tests/e2e/run_case.sh`):
  - The single `.qwe/runs/<id>/` is renamed `RUN/`, with run id and times in `result.json` replaced by `RUN` and `TIME`.
  - Optional `check.sh` for assertions a golden can't express (used for the 10 MiB case).
  - Fixed a bug: the golden-file loop ran in a pipeline subshell, so a mismatch in `expected/<path>` files never failed the test.
- Transcoder only reports `line:col` for parse errors; the position side table is ticket 04. The `!encrypted` tag is ignored (treated as a plain string) until ticket 15. The secret tag in the round-trip test is a placeholder, 32768.
