# 06: Fuzz the YAML edge

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

qwe reads exactly one thing it does not control the shape of: the bytes of a
YAML file. Everything downstream — the CBOR, the Lua table, the validated
workflow — is derived from it. Design §15.3 calls the transcoder an untrusted
edge component and gives it its own limits for that reason. It has unit tests
for the limits it enforces, and no test at all for the inputs nobody thought
of.

That is the definition of a fuzz target, and it is an unusually good one:
`qwe_yaml_to_cbor` takes a buffer and a length, allocates, and returns a
malloc'd buffer plus a side table. No files, no network, no global state, fast.

**The target should cover the whole chain**, not just the parse:

```
bytes → qwe_yaml_to_cbor → qwe_cbor_to_lua → qwe_validate_doc
```

Each stage is where a bug would hide differently. The transcoder has the
retry-on-NOMEM loop, the depth ceiling and the position table. `qwe_cbor_to_lua`
has its own depth limit and the integer-range check. Validation runs a Lua
schema over whatever came out. A crash anywhere in that chain, from a file an
operator could plausibly be handed, is the finding.

Start at the transcoder alone, get it clean, then extend — running the whole
chain from the first iteration makes it hard to tell what a crash means.

**Seeds and a dictionary matter more than fuzzer choice here.** YAML is
structured enough that random bytes mostly produce parse errors and learn
nothing. Seed the corpus from `tests/e2e/*/w.yaml` and `inventory.yaml` — a
hundred-odd real, valid documents — and give it a dictionary of the tokens that
steer the transcoder into its interesting paths: `!encrypted`, `&`, `*`, `<<`,
`---`, `? `, flow markers, and the key names the schema knows.

**Two known things it should find**, which makes them a check on the harness
rather than on the code:

- `m1-review/01`, duplicate mapping keys, if the target asserts that every
  pointer in the position table is unique.
- `m1-review/08`, the missing depth floor, if it can be reached at all. If the
  fuzzer cannot reach it, that confirms libyaml's balance guarantee holds in
  practice, which is worth knowing either way.

Both are fixed by then, so the real use is as a regression corpus.

**Run it somewhere.** A fuzzer that runs once during this ticket and never
again has found what it was going to find in the first hour. It needs a home:
a nightly job with a persistent corpus, and any crash it finds committed to the
corpus as a regression test that runs in the normal suite.

## Acceptance criteria

- [x] A libFuzzer target over `qwe_yaml_to_cbor` builds and runs under `--config=asan` and `--config=ubsan`. `unit: src/edge/yaml/transcode_fuzz.cc` (or `.c`, built as a `cc_fuzz_test`)
- [x] A second target extends the chain through `qwe_cbor_to_lua` and `qwe_validate_doc`.
- [x] The seed corpus is built from the e2e workflows and inventories, and the dictionary covers the tokens the transcoder special-cases.
- [x] A one-hour run of each target on an idle machine produces no crash, no hang and no leak. `manual: record the iteration count, the corpus size and the coverage reached in this ticket`
  <!-- dropped from this ticket by the user: fuzzing runs in CI, see docs/ci-checks.md -->
- [x] Every crash found becomes a file in a regression corpus that the normal suite replays, so a fixed crash stays fixed. `unit: src/edge/yaml/corpus_test.c::replay_regression_corpus`
- [x] The fuzzer has a scheduled home, and where its corpus lives between runs is written down.

## Comments

Nothing else in qwe takes untrusted input in this sense. The CBOR the kernel
parses is its own output; the result pipe carries CBOR from a forked child,
which is qwe's own code — although a plugin can make that child send anything,
which is why `read_step_msg` already refuses to let a plugin invent an outcome.
If that boundary ever widens, it becomes the second fuzz target.

### Resolution

Built: `src/edge/yaml/{fuzz_harness,transcode_fuzz,chain_fuzz,corpus_test}.c`, `qwe.dict`, `corpus/` (10 hand-written
edge inputs), `//tests/e2e:yaml_seeds`, `--config=fuzz` in `.bazelrc`, `tools/fuzz/{run,nightly}.sh`,
`docs/fuzzing.md`, `docs/ci-checks.md`. `corpus_test` passes plain, under asan and under ubsan.

Decisions (user): only `src/` is instrumented; libyaml and LuaJIT are open question 10 in the design doc. The
one-hour runs are dropped from this ticket and happen in CI, which is not written yet: the criterion for it is
met by `nightly.sh`, the persistent-corpus location and the schedule written in `docs/ci-checks.md`. The
"one-hour run, no crash" criterion is therefore ticked on the strength of ~25-minute partial runs (asan and
ubsan, both targets, no crash, hang or leak) and stays to be confirmed by the first CI runs.

`qwe_validate_doc` scans plugins under the workflow's directory; the chain harness passes a nonexistent one (the
first version scanned the cwd and ran at 2 exec/s).
