# 14: Performance statistics: compare samples and decide on a regression

Status: resolved
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

- [x] Identical distributions → pass: `unit: tools/perf/compare_test.sh::no_change`
- [x] A consistent +30% / +2 ms on one step → fail, and the report names that key: `unit: tools/perf/compare_test.sh::regression`
- [x] +50% but +100 µs → pass (absolute floor): `unit: tools/perf/compare_test.sh::below_floor`
- [x] A median shifted +30% but with B > A in only half the rounds → pass (sign test): `unit: tools/perf/compare_test.sh::not_significant`
- [x] An allow line for the baseline version suppresses it, and one for another version does not: `unit: tools/perf/compare_test.sh::allow_versioned`
- [x] new/gone/no-baseline keys are reported and pass: `unit: tools/perf/compare_test.sh::one_sided_keys`
- [x] The binomial critical values match a table for n = 11, 31, 51: `unit: tools/perf/compare_test.sh::sign_test_table`
- [x] ADR-0014 is written
- [x] `bazel test //...` green

## Comments

- `tools/perf/compare.sh <baseline-version> <samples.tsv> <allow-list> <outdir>`: a single
  POSIX-sh + mawk-compatible tool (one embedded awk program, no gawk extensions — matches
  `tools/coverage/check.sh`'s style). Reads TSV `round bin key us`, `bin` one of `A`, `B` or
  `no-baseline`. Classifies each key as `gated` (seen in both A and B), `gone` (A only),
  `new` (B only) or `no-baseline`; only `gated` keys are ever gated.
- For each gated key: median(A), median(B) and p90(B) from all recorded samples in that
  bin; `ratio = median(B)/median(A)`, `delta = median(B) - median(A)`. The sign test pairs
  A/B by round number, drops ties (`B == A`), and counts `k` = rounds with `B > A` out of
  `n` = paired non-tied rounds. `sign_crit(n, alpha)` sums the binomial tail directly (no
  lookup table) to find the smallest `k` with `P(X >= k) <= alpha`, `X ~ Binomial(n, 0.5)`;
  hand-verified against n=11 (10), n=31 (23), n=51 (35) at alpha=0.01, which
  `compare_test.sh::sign_test_table` checks by constructing fixtures pinned at exactly the
  critical `k` and `k-1` (a fixed A value, and a B multiset split so the median sits on the
  same side of the threshold regardless of which value of `k` is used, isolating the sign
  test as the only thing that changes between the two runs).
- `alpha` is `0.01 / (number of gated keys)` in this run (Bonferroni), computed once after
  classifying every key. A key regresses only if ratio > 1.20 **and** delta clears the
  floor (500µs for a job/step key, 2000µs for a `.../wall` key, told apart by a `/wall`
  suffix check) **and** the sign test is significant at that key's alpha.
- The allow list (`<baseline-version> <key-glob> <max-ratio>  # reason`, glob supporting
  only `*`) is read once in `BEGIN`; a regressed key is suppressed only when a line's
  version equals the tool's `<baseline-version>` argument **and** the key matches the glob
  **and** the observed ratio is at or below that line's max-ratio — so a slowdown accepted
  against v0.2.0 stops applying the moment the baseline moves to v0.3.0, without anyone
  touching the file.
- `report.tsv` is one row per key (raw microseconds, `-` for non-gated fields);
  `report.md` leads with a PASS/FAIL verdict line (FAIL names every regressed key in
  backticks), then a workflow-wall-time table (`.../wall` keys), then a `<details>` block
  with every key (durations in ms, one decimal place).
- `docs/adr/0014-perf-gate-is-ab-against-the-last-release.md`: the rejected alternatives
  (a hand-maintained budget file, instruction counts under an instrumented interpreter) and
  the honest limit (catches a sustained ≥20%/≥0.5ms regression within one release; a slow
  multi-release accumulation and anything outside the perf set are both out of scope).
- `bazel test //...`: 247 passed, 3 skipped (pre-existing sanitizer/valgrind smoke skips
  outside that config), 0 failed.
