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
  pools 255 rounds per key of the v0.3.0 binary from five runs on five runners. A runner a
  little faster or slower than the pool moves a wall time by a few percent. Across the six
  real runs checked while this was written, the wall ratios fell between 0.97 and 1.06,
  well inside the 1.20 threshold.
- **The step floor is two whole milliseconds.** Job and step times come from
  `result.json`'s `duration_ms`, which moves in whole milliseconds. A step whose stored
  median is 2 ms reads 3 ms on a slightly slower runner (ratio 1.5). A/B hid that, because
  both binaries shared the runner; against a stored value it looked like a regression. At
  ADR-0014's 500 µs floor, four of those six real runs failed on step keys under
  `smoke_graph`, with nothing changed. At 2 ms (the wall floor too), all six pass, and a
  deliberate 5 ms slowdown in a step still clears it.
- **Drift is fixed by re-measuring, not by loosening.** When the runners change, or code
  gets faster or slower on purpose, `perf-baseline.yml` measures the current code on five
  runners and opens a PR with the new medians and the runners' CPU models. The change is
  reviewed like any other. The allow-list is keyed to the expected file's version, so an
  accepted slowdown expires when the values are re-measured.
- **Only a release gates.** Push and nightly run the same comparison but only report, so
  a runner change never turns `main` red. A release (and a manual dispatch) fails on a
  regression. Before it, the stored values are re-measured if the report has been drifting.

## What stayed the same

The per-key rule: a key regresses only when the median ratio exceeds 1.20, the delta
exceeds its floor, and a one-sided sign test at α = 0.01 (Bonferroni over the gated keys)
finds the candidate above the reference. The sign test is now one-sample, counting rounds
above the stored median, where before it counted paired rounds in which B beat A.

## Consequences

- The perf job runs one binary, about half the time it took.
- A new smoke workflow in the perf set needs expected values before it is gated.
  `//tools/perf:expected_test` fails until they exist, so the gap cannot pass silently as
  a `new` key.
- The expected file records where its numbers came from (runs, runners, version) in its
  header. The first one predates the CPU recording, which it says.
