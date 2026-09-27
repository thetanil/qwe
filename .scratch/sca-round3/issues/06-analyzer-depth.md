# 06: The clang analyzer, deeper

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 04

## What

The doc calls `clang-analyzer-*` "symbolic execution, cross-function". It is
cross-function only inside one translation unit. Three ways to make it look
harder, each a measured experiment, each adopted only if it finds something
real; every new finding is fixed in code (the premise of this round).

1. **Cross-translation-unit analysis.** Without it, a function defined in
   another `.c` is a black box: a `NULL` returned from `qwe_x*`, a buffer freed
   by a callee in another file, an fd closed elsewhere are all invisible.
   `clang-extdef-mapping-20` and `analyze-build` are installed. CTU needs an
   extern-definition map and per-file AST dumps; both can be produced from the
   `bazel aquery` flags `run.sh` already extracts, with no Python (`analyze-build`
   is a Python script: do not use it, produce the map with `clang-extdef-mapping`
   directly and pass `-analyzer-config experimental-enable-naive-ctu-analysis=true`
   and `ctu-dir=`). If it cannot be done without Python or without a fragile
   layout, report that in the Comments and stop this part.
2. **POSIX modelling.** `unix.StdCLibraryFunctions:ModelPOSIX` teaches the
   analyzer the return conventions of `open`, `read`, `write`, `close`, `pipe`,
   `dup2`, `fcntl`. qwe is a fork/exec/pipe program. Check the default first:
   it is believed to have become `true` in clang 19, in which case clang-tidy 20
   already has it on and this part is a no-op to record, not a setting to add
   (`clang-20 -cc1 -analyzer-config-help` or a small test file with a known
   `read` misuse settles it). If it is off, try it via `-Xclang -analyzer-config
   -Xclang unix.StdCLibraryFunctions:ModelPOSIX=true` in `run.sh`'s extra args.
3. **Path-sensitivity settings**: `-analyzer-config` `max-nodes`,
   `max-inlinable-size`, `ipa=dynamic-bifurcate`, `crosscheck-with-z3=true` (only
   if the installed clang was built with Z3; if not, say so). Look at what
   each changes on this tree, not at what the docs promise.

The analyzer's alpha checkers are not exposed by clang-tidy (`--list-checks
--checks='clang-analyzer-*'` lists 286, none `alpha`); running the real
`scan-build`/`clang --analyze` with alpha checkers is out of scope for this
feature.

## Acceptance criteria

- [ ] For each of the three, the number of new findings on this tree, the ones
      that are real bugs, the ones that are not, and a decision to adopt it,
      are in the Comments. `manual: Comments`
- [ ] Each adopted setting is in `run.sh` or `.clang-tidy` with a comment naming
      the ticket, and the gate is green with it. `manual: run.sh at exit 0`
- [ ] Each adopted setting makes the analyzer see more, not less, and is
      recorded as a `widen` row for ticket 28's `tools/ci/exceptions.txt` (list
      them in the Comments; ticket 28 creates the file). A setting that lowers a
      limit to save time (`max-nodes` down) is a `narrow` and is not adopted.
      `manual: Comments`
- [ ] Every new real finding is fixed, with a test where it is an error path.
- [ ] A finding that is a false positive is removed by restructuring the code
      so the analyzer sees the invariant (an early return, a local, an `assert`
      the code keeps), not by a `NOLINT`. If that costs more than a `NOLINT`
      would, say so in the Comments and stop before adopting that setting: an
      analysis mode you must suppress is not worth adopting.
- [ ] `docs/static-analysis.md` says what the analyzer sees (per translation unit
      or across them) and which settings are on. `manual: doc`
- [ ] `bazel test //...` is green.

## Comments
