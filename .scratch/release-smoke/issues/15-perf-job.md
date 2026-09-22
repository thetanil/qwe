# 15: Performance job in smoke.yml: A/B against the last release

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 05, 14

## What

A `perf` job in smoke.yml (needs build; its own fresh runner, no shared setup):

1. **Baseline.** Take the newest release that is not a draft, not a pre-release, and not the input
   `exclude-tag`. Run `gh release download` for its `qwe-<v>-linux-x86_64` and `SHA256SUMS`, then
   `sha256sum -c --ignore-missing`. If there is none, or input `baseline: none`, run report-only and say so
   in the summary. `baseline: self` uses the candidate as A (an A/A calibration).
2. **A/B.** The perf set is the smoke workflows that need no sudo and have no deliberate sleeps (`run`,
   `file`, `graph`, `project_plugin`). One warm-up round is discarded, then `rounds` rounds (input,
   default 51) in ABBA order, alternating by round. Each qwe run records its process wall time and the
   `duration_ms` values from its `result.json` (01) as samples. If the baseline fails a workflow, that
   workflow is `no-baseline`.
3. **Report.** Run the 14 tool. Its `report.md` goes to `$GITHUB_STEP_SUMMARY`, under a header with
   the candidate sha, the baseline tag, the runner's `nproc`/CPU model/kernel, and the rounds. Samples,
   `report.md` and `report.tsv` are uploaded as `perf-<sha>`, with 90-day retention. A regression fails
   the job.

Inputs: `rounds`, `baseline` (`latest`|`self`|`none`), `exclude-tag`, on both dispatch and call. The first
real comparison is against v0.2.0 (`-O0`), so expect a speedup; note it on the ticket.

## Acceptance criteria

- [ ] A push to main: the perf job is green, the summary has the header, the verdict and the tables: `manual: push; paste the header and verdict into a comment`
- [ ] `baseline: self` dispatched 5 times → no regression in any run: `manual: record the 5 run URLs`
- [ ] A scratch branch that adds a 5 ms busy-wait to the `run` plugin's path → the perf job fails and names the affected keys: `manual: workflow_dispatch; record the URL; do not merge`
- [ ] A checksum mismatch in the baseline download fails the job before any run: `manual: scratch branch with a corrupted expected sum`
- [ ] `bazel test //...` green

## Comments
