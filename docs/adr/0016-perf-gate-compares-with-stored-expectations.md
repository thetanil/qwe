# The perf gate compares with stored expectations, not with the last release

Supersedes ADR-0014.

The perf job times the smoke workflows with the candidate binary only, and compares each
key with an expected median stored in the repo, `tools/perf/expected.tsv`. It no longer
downloads the last release and runs it side by side. `tools/perf/expect.sh` writes the
file from pooled samples; `.github/workflows/perf-baseline.yml` re-measures and proposes a
new one as a pull request.

## Why ADR-0014's A/B design was replaced

ADR-0014 chose A/B against the last release so that the baseline would move with the
runner automatically. In practice:

- **An old binary cannot run new workflows.** The v0.3.0 release gated one workflow out of
  four (run 36186458092). The v0.2.0 binary exited 2 on `smoke_run`, `smoke_file` and
  `smoke_project_plugin`, which use features added after it, so those were only reported
  as `no-baseline`. Every release that adds a smoke feature repeats this, and the more the
  project changes, the less the gate sees. A gate that fades as the code moves is not
  stable under change.
- **Half of every run re-measured something already known.** Each round ran both
  binaries, so half the job's time went on a binary that had not changed since its
  release.

## What stored values cost, and how that is handled

ADR-0014 rejected "a budget file" for runner drift, which still applies: GitHub moves its
runners to other CPUs, and a stored median is a number measured on some other day's
hardware. The handling:

- **The expected values are medians pooled over several runners, not one.** The first file
  pools 255 rounds per key of the v0.3.0 binary from five runs on five runners.
- **A key fails only at more than 2x its expected median.** The first `perf-baseline.yml`
  dispatch (run 36308620510) measured one binary on five runners with four CPU models. The
  `smoke_run` wall medians were 50.6 ms (AMD EPYC 9V45), 62.3 and 63.7 ms (EPYC 9V74),
  81.4 ms (EPYC 7763) and 85.0 ms (Intel Xeon 8370C): 1.68x apart, with no code change,
  and the other workflows spread the same way. Against a pooled median, the 1.20 threshold
  first chosen would have failed a release that landed on a slow runner, for nothing. On
  timings this short, runner variance is expected, so the tolerance says so: 2x. The price
  is that a slowdown has to double a key before it fails, and a fast runner can hide a
  somewhat larger one. That is accepted. The gate catches gross regressions (an
  accidental sleep, a lost cache, a quadratic loop); the per-key numbers in every report
  are there for anyone who wants to look at smaller ones.
- **The step floor is two whole milliseconds.** Job and step times come from
  `result.json`'s `duration_ms`, which moves in whole milliseconds. A step whose stored
  median is 2 ms reads 3 ms on a slightly slower runner (ratio 1.5). A/B hid that, because
  both binaries shared the runner; against a stored value it looked like a regression. At
  ADR-0014's 500 µs floor and 1.20 ratio, four of six real runs failed on step keys under
  `smoke_graph`, with nothing changed. The floor is 2 ms, the same as the wall floor.
- **Re-measuring keeps the reference current.** When code gets faster or slower on
  purpose, or the runner fleet changes, `perf-baseline.yml` measures the current code on five
  runners and opens a PR with the new medians and the runners' CPU models. The change is
  reviewed like any other. The allow-list is keyed to the expected file's version, so an
  accepted slowdown expires when the values are re-measured.
- **Only a release gates.** Push and nightly run the same comparison but only report, so
  a runner change never turns `main` red. A release (and a manual dispatch) fails on a
  regression. Before it, the stored values are re-measured if the report has been drifting.

## The per-key rule

A key regresses only when the median ratio exceeds 2.0, the delta exceeds its floor, and
a one-sided sign test at α = 0.01 (Bonferroni over the gated keys) finds the candidate
above the reference. The sign test is one-sample, counting rounds above the stored median,
where ADR-0014 counted paired rounds in which B beat A.

## Rejected

- **Expected values per CPU model.** They would tighten the tolerance only for the
  variance a CPU model explains (the two EPYC 9V74 runners agreed within about 2%). The
  fleet changes without notice, and a runner whose model has no values yet could not be judged
  at all. The 2x tolerance covers runner variance of every kind, with one set of values.

## Consequences

- The perf job runs one binary, about half the time it took.
- A new smoke workflow in the perf set needs expected values before it is gated.
  `//tools/perf:expected_test` fails until they exist, so the gap cannot pass silently as
  a `new` key.
- The expected file records where its numbers came from (runs, runners, version) in its
  header. The first one predates the CPU recording, which it says.
