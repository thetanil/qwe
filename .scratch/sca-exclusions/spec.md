# Static analysis: the remaining exclusions

Status: ready-for-agent

## What

`sca-findings` (archived) turned the bug-finding exclusions back on. What is
left in `.clang-tidy` is 17 excluded checks, plus one narrowing:
`cert-err33-c.CheckedFunctions`, which leaves out every
`fprintf`/`fputs`/`fputc`. Each exclusion carries a "why excluded" row in
`docs/static-analysis.md`. This feature removes as many as can be removed
honestly: fix the code, or turn a blanket exclusion into a narrow option. Where
a row's reasoning still holds, the ticket closes `wontfix` with evidence.

Measured on 2026-09-26 at `2d56de5`, with the raw ruleset (every group on, no
exclusions, **and no `CheckOptions`**): **427 findings**. `run.sh --raw` reports
254, because it keeps `CheckOptions` and so hides the 173 `cert-err33-c`
findings. Ticket 01 fixes that first, so every later ticket can measure its
class.

## Order

The narrowing and the measurement come first:

1. **01** makes `--raw` see what a `CheckOptions` narrowing hides.
2. **02** removes an exclusion that now has nothing left to flag.
3. **03** and **04** work `cert-err33-c` down to its default list, then delete
   `CheckedFunctions`.
4. **05** to **09** handle the other exclusions, largest first. **05** and **06**
   replace a blanket exclusion with a narrow option where clang-tidy has one.

## Classes

| Ticket | Check(s) | Findings | Direction |
|---|---|---|---|
| 01 | `run.sh --raw` itself | n/a | drop `CheckOptions` in raw mode; 254 → 427 |
| 02 | `bugprone-unsafe-functions`, `cert-msc24-c`, `cert-msc33-c` | 0 | stale: `sca-findings/06` removed the last `rewind` |
| 03 | `cert-err33-c` on data streams (`fprintf`/`fputs`/`fputc` to a file) | 83 | lost data; `summary.c` never checks `ferror` |
| 04 | `cert-err33-c` on `stderr`; delete `CheckedFunctions` | 90 | one diagnostic idiom |
| 05 | `bugprone-reserved-identifier`, `cert-dcl37-c`, `cert-dcl51-cpp` | 74 | `AllowedIdentifiers` for the five required names |
| 06 | `concurrency-mt-unsafe` | 79 (49 under `FunctionSet: glibc`) | narrow, fix, or enforce "no threads" |
| 07 | `assignment-in-if-condition`, `implicit-widening-of-multiplication-result`, `misplaced-widening-cast`, `optin.performance.Padding` | 7 + 6 + 2 + 1 | small code fixes |
| 08 | `cert-msc30-c`, `cert-msc32-c`, `cert-msc50-cpp`, `cert-msc51-cpp` | 5 + 1 | all in `sched_test.c`: a local PRNG |
| 09 | `multi-level-implicit-pointer-conversion`, `easily-swappable-parameters` | 50 + 29 | re-verify; narrow via options or `wontfix` |

## Constraints

The same constraints as `sca-findings`:

- No Python (`CLAUDE.md`).
- `third_party/` is not edited.
- A fix is a code change. A `NOLINTNEXTLINE(<check>)` is allowed only for a
  single proven false positive, with a comment saying why.
- A class that turns out to be all false positives, or all a required idiom,
  stays excluded. Its ticket closes `wontfix` with the evidence in its comments,
  and its doc row is updated to match.
- Each ticket ends with `tools/clang-tidy/run.sh` at exit 0, `bazel test //...`
  green, and the coverage check green (`bazel run //tools/coverage:check`, every
  file at least 85%). Its row in `docs/static-analysis.md` is removed or
  rewritten, and any real bug is listed under "What re-enabling excluded checks
  found".
- A new error branch gets a test that reaches it. Lowering the coverage floor
  is not a fix (see `docs/coverage.md`, "Every file at least 85%").

## Tickets

- [01: `--raw` sees what `CheckOptions` narrows](issues/01-raw-drops-check-options.md)
- [02: remove the stale `rewind` exclusion](issues/02-stale-rewind-exclusion.md)
- [03: `cert-err33-c` on data streams](issues/03-err33-data-streams.md)
- [04: `cert-err33-c` on diagnostics; delete `CheckedFunctions`](issues/04-err33-diagnostics.md)
- [05: reserved identifiers: an allow list, not an exclusion](issues/05-reserved-identifier-allow-list.md)
- [06: `concurrency-mt-unsafe`](issues/06-mt-unsafe.md)
- [07: small bugprone classes](issues/07-small-bugprone-classes.md)
- [08: `rand` in `sched_test`](issues/08-rand-in-sched-test.md)
- [09: re-verify the two style exclusions](issues/09-style-exclusions.md)
