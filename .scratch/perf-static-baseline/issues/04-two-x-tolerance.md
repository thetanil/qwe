# 04: Fail only at 2x the expected value

Status: resolved
Category: enhancement
Type: task

## What

The first `perf-baseline.yml` dispatch (run 36308620510, PR #3) measured one binary on
five runners with four CPU models. The `smoke_run` wall medians were 50.6 ms (EPYC 9V45),
62.3 and 63.7 ms (EPYC 9V74), 81.4 ms (EPYC 7763) and 85.0 ms (Xeon 8370C): a 1.68x
spread with no code change. The other workflows spread the same way. Against a pooled
median, the 1.20 ratio threshold fails a release that lands on a slow runner, for nothing.

Decided with the user on 2026-09-27: no per-CPU expectations. Runner variance on timings
this short is expected, and tighter tolerances only pretend otherwise. A key fails only
when its median is more than 2x the stored median (the delta floors and the sign test
still apply too).

## Acceptance criteria

- [x] `compare.sh`'s ratio threshold is 2.0. `unit: tools/perf/compare_test.sh`
      (regression at more than 2x fails; 1.9x passes)
- [x] ADR-0016 records the runner data and the 2x rule. `manual: docs/adr/0016-*.md`
- [x] `bazel test //...` is green.

## Comments

2026-09-27: done.

- `RATIO_THRESHOLD=2.0`, with the reason in `compare.sh`'s comment. The step and wall
  floors (2 ms) and the sign test are unchanged.
- Tests: `regression` now uses 2.1x, a new `under_two_x` case (1.9x, +9 ms, every round)
  must pass, and `allow_versioned`'s line allows up to 3.0x. Under the old 1.20 threshold,
  `under_two_x` would have failed.
- Checked on real data: each of run 36308620510's five runners (all four CPU models)
  passes against both the current v0.3.0 values and PR #3's pooled v0.3.0-m2 values. The
  highest ratio was exactly 2.0, on 1 ms step keys reading 2 ms, which is not more than
  2x and is under the 2 ms delta floor.
- ADR-0016 records the runner spread, the 2x rule and its price (a slowdown has to
  double a key), and rejects per-CPU-model expectations.
- Also fixed: `perf-baseline.yml`'s PR description labelled keys whose old median was 0 ms
  as `new` (PR #3 shows it). They now read `-`.
- `.scratch/release-smoke-verification`'s busy-wait check is updated: 5 ms no longer
  doubles a step, so use about 50 ms.
- `bazel test //...`: 262 pass, 3 skipped.
