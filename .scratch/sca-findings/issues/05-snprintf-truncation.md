# 05: snprintf/sprintf truncation

Status: resolved
Category: bug
Type: task
Blocked by: 04

## What

Add `::snprintf;::sprintf` to ticket 04's `cert-err33-c` `CheckedFunctions`. There
are 96 hits.

- **`sprintf`, 5 hits:** `src/kernel/validate.c` (3) and `src/kernel/workflow.c` (2).
  These have no bound at all. Convert each one to a bounded call.
- **`snprintf` in production code:** `secrets/keyfile.c` (11),
  `edge/yaml/transcode.c` (8), `kernel/workflow.c` (5), `luacbor.c` (4),
  `dag.c` (3), `trace.c` (2), `sink.c`, `validate.c`, `tools/bcembed.c`. A
  truncated path or key name is a wrong answer, not a cosmetic one, so each
  of these either checks `ret < 0 || ret >= size` and fails, or has a comment
  proving it cannot truncate.
- **`snprintf` in tests, about 50 hits:** mostly building fixture paths. A small
  helper that asserts no truncation is likely cheaper than 50 inline checks.

If the same check-and-fail pattern repeats across `src/`, a single
`qwe_snprintf_checked` (or similar) helper in `src/kernel/` is the right
shape. Then the check sees one checked call instead of dozens.

## Acceptance criteria

- [x] No `sprintf` in `src/` or `tools/`. `manual: grep -rnw sprintf src tools finds only snprintf/vsnprintf`
- [x] `CheckedFunctions` includes `snprintf` and `sprintf`, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] At least one production truncation path is exercised: an over-long input is rejected with an error. `unit: keyfile_test's generate_refuses_an_overlong_path`; `e2e: tests/e2e/keygen_long_home/`
- [x] Real bugs are listed under "What the gate found". `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

- 97 hits at `e0524c7`. The shape is `src/kernel/fmt.h` (`//src/kernel:fmt`, header-only, with its own `fmt_test`). It has three helpers, chosen by what a cut means, and each call site's name says which one applies:
  - `qwe_fmt` returns -1 on truncation. For paths, keys and names: `keyfile.c`'s parent-directory copy and `qwe_key_default_path`.
  - `qwe_xfmt` aborts with `file:line: formatted text truncated`, like `qwe_xmalloc`. For buffers sized to fit: `sink.c`'s log path and line prefix, `trace.c`'s step number and `E<errno>`, `workflow.c`'s run id (a comment proves 64 bytes fit), ring alarm, run dir and trace path, and `validate.c`'s JSON pointers. Also every test fixture path (47 sites), which matches ticket 03's "setup aborts" idiom.
  - `qwe_msg` returns void, for diagnostic text: 30 error-message sites in `transcode.c`, `dag.c`, `luacbor.c`, `keyfile.c`, `validate.c` and `workflow.c`, plus `lifecycle_test`'s two failure-message buffers.
- Real bugs:
  - `qwe_key_generate` copied `path` into `dir[1024]` unchecked and `mkdir`'d every parent of the truncated copy. Now: "the key file path is too long". Test `generate_refuses_an_overlong_path` (red before, green after) also checks that no directory was made.
  - `qwe keygen` and `resolve` reported an over-long `HOME` as "HOME is not set". Now: "HOME is too long". Test: e2e `keygen_long_home`.
  - `bcembed` passed an unchecked `malloc` to `sprintf` for a JSON module, and a module name over 255 bytes was silently cut in its chunk name. Both now exit 1.
- `sprintf`: the five hits plus `sink.c:48`, which the check could not see because its return was used, are gone. `grep -rnw sprintf src tools` now finds only awk's `sprintf` in `tools/perf/compare.sh`, which is not C.
- Gate exits 0. `bazel test //...` is green (251 pass, 3 skipped).
