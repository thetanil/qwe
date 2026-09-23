# 07: Unbounded strcpy and tainted allocation

Status: resolved
Category: bug
Type: task
Blocked by: 01

## What

**`clang-analyzer-security.insecureAPI.strcpy` (3 hits):**
`src/kernel/workflow.c:444` (`load_inventory`), `src/kernel/workflow.c:1900`
(the run-directory path) and `src/kernel/redact_test.c:52`. The doc
says each one is bounded by hand-checked arithmetic. With only three sites,
replacing them with `memcpy` on the length already computed, or with a checked
`snprintf` (ticket 05's helper if it exists), costs less than keeping a
name-based exclusion that would let a fourth, unchecked call through.

**`clang-analyzer-optin.taint.TaintedAlloc` (1 hit):** `tools/bcembed.c:88`, an
allocation sized from file contents or argv. It is a build-time tool, but a
size check against a sane maximum (the largest embedded module times some
margin) is one line and makes the finding go away honestly.

## Acceptance criteria

- [x] No `strcpy`/`strcat` in `src/` or `tools/`. `manual: grep -rnwE 'strcpy|strcat' src tools finds nothing`
- [x] `bcembed` rejects an input over its size cap with an error. `unit: //tools:bcembed_test` (the harness ticket 04 added)
- [x] `.clang-tidy` no longer excludes either check, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] Both rows are gone from the exclusion table. `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

- With the exclusion off there were four hits, not three. `trace.c:53` (`strcpy(stepbuf, "-")`) was missing from the list, and the run-directory site is a `strcat` (`workflow.c`, `"/result.json"`), not a `strcpy`. Each fix uses a length already in hand:
  - `load_inventory`: `memcpy(default_path + dir_len, "inventory.yaml", sizeof "inventory.yaml")`, into the buffer allocated as `dir_len + sizeof "inventory.yaml"`.
  - run directory: `qwe_xfmt(run_dir + strlen(run_dir), run_dir_size - strlen(run_dir), "/result.json")`, using ticket 05's `run_dir_size`, whose comment says its 32 spare bytes include `/result.json`.
  - `trace.c`: `memcpy(stepbuf, "-", sizeof "-")`.
  - `redact_test.c`: `memcpy` of `strlen(whole + cut) + 1` (13 bytes into 32).
- `TaintedAlloc`: `bcembed`'s `slurp` refuses a source over `BCEMBED_MAX_SOURCE` (1 MiB) with `errno = EFBIG`, before `malloc`. `main` now prints `strerror(errno)` on a read failure. The largest real input is luacheck's `parser.lua` at 31 KB, so the cap is about 30 times that. The `bcembed_test` case (a 1 MiB + 1 byte file of `-`, which is valid Lua) exited 0 before the fix and exits 1 with `cannot read <file>: File too large` after.
- Gate exits 0 with both checks on. `grep -rnwE 'strcpy|strcat' src tools` finds nothing. `bazel test //...` is green (252 pass, 3 skipped).
