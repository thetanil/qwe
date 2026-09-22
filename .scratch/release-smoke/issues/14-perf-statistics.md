# 14: Performance statistics: compare samples and decide on a regression

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

A shell + awk tool (POSIX awk: the devcontainer has mawk; no Python) that takes A/B timing samples and
decides whether the candidate (B) is slower than the baseline (A).

- Input: TSV lines `round  bin(A|B)  key  us`. Keys look like `<workflow>/wall`, `<workflow>/<job>`,
  `<workflow>/<job>/<step-index>`, and may carry a `no-baseline` marker for a workflow the baseline could
  not run.
- For each key: the A median, the B median, B's p90, the ratio, the delta, and the number of rounds
  where B > A (paired by round).
- A key **regresses** only if all three hold:
  1. the median ratio is > 1.20;
  2. the median delta is > 500 µs for jobs and steps, or > 2000 µs for `wall`;
  3. the paired one-sided sign test is significant at α = 0.01 / (number of gated keys), a Bonferroni
     correction, with the binomial tail computed in awk.
  The thresholds are constants at the top of the tool.
- An allow list: lines `<baseline-version> <key-glob> <max-ratio>  # reason`. A line applies only when
  the baseline version equals its version, so an accepted slowdown expires at the next release.
- Keys only in A (`gone`) or only in B (`new`), and `no-baseline` workflows, are reported, never gated.
- Output: `report.md` (the verdict first, a per-workflow wall table, then per-step details in
  `<details>`, with every duration in ms), `report.tsv`, and exit 1 on any regression.

Record the method in `docs/adr/0014-perf-gate-is-ab-against-the-last-release.md`: the rejected
alternatives (an absolute budget file, instruction counts) and the honest limit (it catches a sustained
slowdown of ≥ 20% and ≥ 0.5 ms; smaller drifts can build up across releases).

## Acceptance criteria

- [ ] Identical distributions → pass: `unit: tools/perf/compare_test.sh::no_change`
- [ ] A consistent +30% / +2 ms on one step → fail, and the report names that key: `unit: tools/perf/compare_test.sh::regression`
- [ ] +50% but +100 µs → pass (absolute floor): `unit: tools/perf/compare_test.sh::below_floor`
- [ ] A median shifted +30% but with B > A in only half the rounds → pass (sign test): `unit: tools/perf/compare_test.sh::not_significant`
- [ ] An allow line for the baseline version suppresses it, and one for another version does not: `unit: tools/perf/compare_test.sh::allow_versioned`
- [ ] new/gone/no-baseline keys are reported and pass: `unit: tools/perf/compare_test.sh::one_sided_keys`
- [ ] The binomial critical values match a table for n = 11, 31, 51: `unit: tools/perf/compare_test.sh::sign_test_table`
- [ ] ADR-0014 is written
- [ ] `bazel test //...` green

## Comments
