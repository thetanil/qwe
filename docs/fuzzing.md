# Fuzzing the YAML edge

Two libFuzzer targets in `src/edge/yaml/`, both driven by `fuzz_harness.c`:

| Target | Chain |
|---|---|
| `transcode_fuzz` | bytes → `qwe_yaml_to_cbor` (CBOR well-formed, duplicate-key positions ordered) |
| `chain_fuzz` | the above → `qwe_cbor_to_lua` → `qwe_validate_doc` (no plugin dir is read from disk) |

Only qwe's own code (`src/`) is instrumented for coverage; libyaml and LuaJIT are
not (open question 10 in the design doc).

```
tools/fuzz/run.sh <transcode|chain> [seconds] [asan|ubsan]
```

Needs clang. The script builds with `--config=fuzz --config=<san>`, seeds the
corpus from `tests/e2e/*/w.yaml`, `inventory.yaml` and `src/edge/yaml/corpus/`,
and uses `src/edge/yaml/qwe.dict`.

**Where the corpus lives between runs:** `$QWE_FUZZ_DIR/<target>` (default
`~/.cache/qwe-fuzz/<target>`); crashes go to `<target>-crashes/` beside it. A
scheduled run should point `QWE_FUZZ_DIR` at a persistent volume.

**Regression corpus:** a crash, once understood and fixed, is copied into
`src/edge/yaml/corpus/`. `//src/edge/yaml:corpus_test` replays that directory and
every e2e workflow through both entry points in the normal suite.

**Home:** `fuzz.yml`: the nightly calls it for 3600 s, or start it by hand (never a push or tag; the release
does not call it):

```
gh workflow run fuzz.yml -f seconds=3600
```

`seconds` is at most 19800. The job runs `tools/fuzz/nightly.sh` (kept under that
name), which fuzzes both targets under `asan` and `ubsan` in parallel against the
persistent corpus and exits non-zero if any crash artifact exists. The corpus
(`$QWE_FUZZ_DIR`, `.fuzz/` in the workspace) is restored from and saved to the
Actions cache under `fuzz-corpus-<run id>`, crashes excluded. Crashes and the
`.log` files are uploaded as the `fuzz-findings` artifact (`include-hidden-files`:
`.fuzz/` is a dot-directory, which `upload-artifact` otherwise skips). For each
process that crashed, `nightly.sh` prints its artifacts and the last 60 lines of
its log (the sanitizer report) in the Fuzz step, and adds an error annotation; the job summary lists
each process's executions, corpus size and coverage.

**Leak checking and the Lua heap:** `chain_fuzz` keeps one Lua state for the
process's life, and LeakSanitizer (on under `asan`) cannot see into LuaJIT's own
mmap arena. PCRE2 objects that lua-schema keeps in that state looked leaked, and
every nightly from the `pattern:` keyword (ticket 17) until 2026-09-25 failed with
one `leak-*` artifact. `--config=fuzz` now builds LuaJIT with
`LUAJIT_USE_SYSMALLOC` (`--define=qwe_fuzz=1`, `third_party/luajit/BUILD`), so the
Lua heap is malloc'd and scanned. The harness also turns the JIT off for that
state: the trace compiler holds its IR buffer by a biased pointer LeakSanitizer
cannot follow, and the chain, not LuaJIT's compiler, is what is fuzzed.

Numbers from a first one-hour run: not recorded yet.
