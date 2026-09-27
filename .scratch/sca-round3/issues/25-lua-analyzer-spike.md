# 25: A second Lua analyzer, evaluated

Status: ready-for-agent
Category: enhancement
Type: spike
Blocked by: 23

## What

luacheck is a linter, not a type checker. Whether a second tool earns its place
is an empirical question; this ticket answers it, and adopts nothing unless it
finds something real and can run clean without a suppression.

Candidates, and what excludes them up front:

- **lua-language-server (`--check`)**: infers types and reports mismatched calls,
  nil-dereferences, and undefined fields. C++ binary, needs a runtime download
  in CI, so it is one more tool to pin and keep evidence for (ticket 04). LuaJIT's `ffi` and the
  `qwe.*` C modules would need `.luarc.json` stubs.
- **selene**: Rust; same class as luacheck. Try it only to see whether it finds
  anything luacheck does not.
- **Teal (`tl check`)**: needs type annotations; a rewrite, not a check.
- **Semgrep**: Python tool; excluded by `CLAUDE.md`.
- **CodeQL**: no Lua support.

The premise of this round is that a second analyzer with 50 findings to argue
about is a cost, not an asset. Adopt only one that is clean after code fixes
(no `---@diagnostic disable`).

## Method

Run each on `src/kernel/lua/`, `src/kernel/schema/`, `plugins/builtin/`; record the
findings; triage each as a true bug, a real style problem, or noise; fix the
true ones; count how many *luacheck would not have found*. Compare the setup
cost (a binary to pin, stubs to maintain) with the yield.

## Acceptance criteria

- [ ] A table in the Comments: tool, version, findings, true positives, noise,
      luacheck overlap. `manual: Comments`
- [ ] A decision for each: adopted (then wired as a Bazel or CI check, pinned,
      with zero suppressions and a fixed version in the doc) or rejected (one line
      why). `manual: Comments`
- [ ] Any true positive is fixed with a test.
- [ ] `docs/static-analysis.md` gets a "Lua" section: which tools, what version,
      what they cover. `manual: doc`
- [ ] `bazel test //...` is green.

## Comments
