# 29: Closeout: the register, and everything rerun

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 02, 03, 04, 05, 06, 07, 08, 09, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28

## What

`docs/static-analysis.md` grew by accretion over three features: a ruleset
section, a table of "why excluded", three idiom sections, two long lists of what
was found, and a history of numbers. It is written for whoever did the work.
A safety assessor needs a different document: what analysis is done, with which
tool at which version, what it covers, and what is left over and why.

## Fix

Rewrite the doc into these parts, and retire the history:

1. **What runs, where, when, at which version:** clang-tidy (pinned), the GCC
   analyzer (if adopted), compiler warnings, CodeQL, luacheck (and any second Lua
   tool), the sanitizers and valgrind (link `docs/sanitizers.md`,
   `docs/valgrind.md`), the fuzzers, coverage. One table, each row with the CI
   workflow that runs it and the artifact it produces.
2. **Coverage of the analysis:** every file, every configuration, every
   language (tickets 05, 23), and what is deliberately not analyzed (vendored
   code, with a link to the third-party register, `docs/third-party.md`).
3. **The exception register:** every remaining exclusion, option, `NOLINT`,
   allow-list entry, and `(void)` idiom, one row each, with its ticket and the
   reason. It should be short. It is generated or checked against
   `tools/ci/exceptions.txt` (ticket 28) so it cannot drift.
4. **The idioms** (`qwe_fmt`/`qwe_xfmt`/`qwe_msg`, `qwe_out`, `qwe_diag`,
   `qwe_strv`, the test `qwe_own`): a short reference for a developer, not a
   history.
5. **How to reproduce a finding:** `run.sh`, `run.sh --raw`, the GCC config.

Move "what the gate found" into `docs/adr/` or an appendix, kept short: the bugs
are useful evidence for an assessor (the analysis found real defects), but the
running commentary is not.

Then the rerun, all of it, on a clean checkout with the pinned tools, and paste
the output into this ticket:

- `tools/clang-tidy/run.sh` exit 0 and `--raw` with "hidden by an exclusion: 0; by
  a CheckOptions narrowing: 0" (except what is in the register);
- `bazel build --copt=-fanalyzer //src/... //tools/...`: zero `-Wanalyzer-*`;
- the warning flags of ticket 08/09: zero;
- luacheck over all Lua: zero;
- CodeQL: zero open alerts;
- `bazel test //...`, `--config=asan`, `--config=ubsan`, `--config=valgrind`,
  coverage;
- `grep -rn NOLINT src tools`.

Then close the feature per `docs/agents/issue-tracker.md`, "Closing a feature":
promote what is load-bearing, repoint references, and `git mv` both
`.scratch/sca-exclusions` (already closed, still in place; its `09` `wontfix` is
superseded by tickets 10-12 here) and `.scratch/sca-round3` to
`.scratch/archive/`.

## Acceptance criteria

- [ ] The doc has the five parts above and no history. `manual: read it`
- [ ] The exception register in the doc equals the `narrow` rows of
      `tools/ci/exceptions.txt`, and the `widen` rows are listed as
      configuration in part 1. `unit: tools/ci/no_exceptions_test.sh`
- [ ] Nothing in `.scratch/iso26262-tool-qualification` is a precondition:
      this feature closes on its own tickets (ADR-0015, "Consequences").
      `manual: the Blocked by line above names no ticket outside this feature`
- [ ] Every command in the "rerun" list, with its output, is in the Comments.
      `manual: Comments`
- [ ] Both features are archived and `grep -rn ".scratch/sca-"` outside
      `.scratch/` finds no live path. `manual: grep`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
