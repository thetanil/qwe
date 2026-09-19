# 03: Tracer bullet: `qwe run` executes one `run:` step locally

Status: ready-for-agent
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

- [ ] A workflow with `run: echo hello` prints `[j] hello`, writes `hello` to `j.log`, writes a `result.json` with step and job `success`, and exits 0. `e2e: tests/e2e/tracer_hello/`
- [ ] `run: exit 3` gives step and job `failed` with reason `exit-code`, and qwe exits 1. `e2e: tests/e2e/tracer_exit_code/`
- [ ] stdout and stderr are merged into one log in the order they arrived. `e2e: tests/e2e/tracer_stderr_merged/`
- [ ] The step's child is its own process-group leader (its pgid equals its pid, and differs from qwe's pgid). `unit: src/kernel/proc_test.c::child_is_group_leader`
- [ ] 10 MiB of output from a step doesn't stall the loop, and every byte reaches the log (ring drains within the 1 MiB capacity). `e2e: tests/e2e/tracer_large_output/`
- [ ] The ring records drops when the reader falls behind and never overwrites without counting. `unit: src/kernel/ring_test.c::drop_accounting`
- [ ] TinyCBOR `v7.0` is vendored in `third_party/tinycbor/` as a plain `cc_library` (its two CMake-generated headers written by hand), builds with strict C99, and round-trips a map, an array, a byte string and a value carrying the secret tag. `unit: third_party/tinycbor/roundtrip_test.c::tagged_value_survives` (moved from ticket 02)
