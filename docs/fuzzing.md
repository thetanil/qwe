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

**Scheduled home:** CI. `tools/fuzz/nightly.sh` runs both targets under `asan` and
`ubsan` in parallel (one hour each by default) against the persistent corpus and
exits non-zero if any crash artifact exists. The CI job is not written yet; it
must persist `$QWE_FUZZ_DIR` between runs.
