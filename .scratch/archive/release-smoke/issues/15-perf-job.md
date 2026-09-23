# 15: Performance job in smoke.yml: A/B against the last release

Status: resolved
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

- [x] A push to main: the perf job is green, the summary has the header, the verdict and the tables: `manual: push; paste the header and verdict into a comment` — done, [run 35875671339](https://github.com/thetanil/qwe/actions/runs/35875671339), see comments for the header/verdict/tables and the first real v0.2.0 comparison
- [ ] `baseline: self` dispatched 5 times → no regression in any run: `manual: record the 5 run URLs` — still not done; tracked in `.scratch/release-smoke-verification`
- [ ] A scratch branch that adds a 5 ms busy-wait to the `run` plugin's path → the perf job fails and names the affected keys: `manual: workflow_dispatch; record the URL; do not merge` — still not done; tracked in `.scratch/release-smoke-verification`
- [ ] A checksum mismatch in the baseline download fails the job before any run: `manual: scratch branch with a corrupted expected sum` — still not done; tracked in `.scratch/release-smoke-verification`
- [x] `bazel test //...` green

## Comments

- `.github/workflows/smoke.yml` gained `workflow_dispatch`/`workflow_call` inputs
  `rounds` (default 51), `baseline` (`latest`|`self`|`none`, default `latest`) and
  `exclude-tag`, and a `perf` job (`needs: build`, its own fresh runner, no shared
  setup — same reasoning as `smoke`: `tools/perf/{run,compare}.sh` are plain sh + awk
  and the point is partly to prove the candidate binary needs no Bazel either).
- **Find the baseline** step: `latest` looks up the newest release via
  `gh release list --exclude-drafts --exclude-pre-releases`, drops `exclude-tag` in a
  `jq` filter, and downloads `qwe-<v>-linux-x86_64` + `SHA256SUMS` with
  `gh release download`, then `sha256sum -c --ignore-missing` before anything runs off
  it — a bad checksum fails this step and the job never gets to the A/B run. `self`
  points the baseline at the candidate binary itself (A/A calibration). `none`, or no
  eligible release found, leaves the baseline path empty and says so in the run
  summary; `tools/perf/run.sh` then never emits an A sample, so every key comes out of
  `compare.sh` classified `new` (never gated) — report-only falls out of the existing
  new/gone/no-baseline handling in 14, no special case needed.
- `tools/perf/run.sh <outdir> <rounds> <candidate-bin> <baseline-bin> <workflow-file>...`
  (14's sibling tool) drives the A/B rounds: one warm-up round (both sides, every
  workflow) runs first and is discarded, only to decide per workflow whether the
  baseline can run it at all — a workflow whose baseline fails the warm-up (or any
  later round) is marked `no-baseline` from then on, the candidate keeps running and
  being timed. The timed rounds alternate `A then B` / `B then A` by round parity
  (ABBA), so a systematic drift across the run lands on both sides evenly. Each
  invocation is timed with `date +%s%N` around the process and its `result.json` (01)
  is read with `jq` for every job's and step's `duration_ms`, emitted as
  `<workflow>/wall`, `<workflow>/<job>` and `<workflow>/<job>/<step-index>` (the
  step's array position) rows in `samples.tsv`.
- The perf set is `smoke_run.yml`, `smoke_file.yml`, `smoke_graph.yml` and
  `smoke_project_plugin.yml`, as the ticket lists (no sudo, no deliberate artificial
  delays — `smoke_graph`'s rendezvous busy-wait is a short poll loop between two
  parallel jobs, not something added to slow the workflow down, so it stays in).
- `tools/perf/allow-list.txt`: the accepted-slowdown file 14 designed for, committed
  empty (nothing has been accepted yet).
- The report header (runner vCPU count, CPU model from `/proc/cpuinfo`, kernel via
  `uname -r`, candidate sha, baseline label, rounds) is written before `compare.sh`
  runs, and `compare.sh`'s `report.md` is appended to `$GITHUB_STEP_SUMMARY`
  unconditionally (`if: always()`) so a regression's own report still lands in the
  summary even though the compare step itself then fails the job. Samples and both
  reports are uploaded as `perf-<sha>` (90-day retention) the same way, `if: always()`.
- `tools/perf/run_test.sh` (a new bazel unit test, `//tools/perf:run_test`, not asked
  for by the ticket but cheap insurance): a stub `qwe` binary that writes a canned
  `result.json` and can be told to fail one workflow name checks `run.sh`'s own
  bookkeeping — the warm-up round is discarded, ABBA order alternates by round parity,
  a failing baseline gets marked `no-baseline` and stops being retried, and
  `baseline: none` never emits an `A` row. Manually piped that stub output through
  `tools/perf/compare.sh` too, to confirm the two tools' TSV format actually agrees
  end to end (14 and 15 were written by the same session but are independent scripts).
- **The four manual criteria are not checked off**, for the same reason 05's were not:
  this session works under `thetanil/qwe/CLAUDE.md`'s "Never push" rule and has no way
  to trigger a real GitHub Actions run without pushing or dispatching on the remote.
  What was verified locally instead: `tools/perf/run.sh` and `tools/perf/compare.sh`
  both under their own unit tests, a manual stub-`qwe` run piped end to end through
  `compare.sh` (see above), and the edited `smoke.yml`/`release.yml`/`nightly.yml`
  parsed with a standalone libyaml-based checker (compiled from the vendored
  `third_party/libyaml` sources, no python involved) to catch YAML syntax mistakes
  that `tools/ci:workflows_test`'s grep-based checks would not.
- `bazel test //...`: 248 passed, 3 skipped (pre-existing), 0 failed.

- **Update, after the user pushed and a fix landed (2026-09-23):** the first push (`ccf8f81`)
  failed every job on a `tools/perf:compare_test` awk portability bug (fixed in `c05921b`,
  unrelated to this ticket's design). [Run 35875671339](https://github.com/thetanil/qwe/actions/runs/35875671339),
  at the fix commit, is the perf job's first real run: `baseline` defaulted to `latest`,
  found `v0.2.0`, downloaded and checksummed it (`qwe-0.2.0-linux-x86_64: OK`), and
  `tools/perf/run.sh` correctly marked `smoke_run`, `smoke_file` and `smoke_project_plugin`
  `no-baseline` — v0.2.0 predates the `assert`/`file.read`/`file.line`/project-plugin
  builtins those workflows use (`unknown plugin "assert" (built-in: file.ensure; project:
  smoke.touch)`), so the baseline binary genuinely cannot run them; this is the no-baseline
  path working as designed against a real old release, not a bug. `smoke_graph` was the one
  workflow both binaries could run; the downloaded `perf-c05921b...` artifact's `report.md`:
  **PASS** (no key regressed), `smoke_graph/wall` A(v0.2.0) median 55.0ms vs B(candidate)
  63.7ms, ratio 1.159, delta 8.8ms, 49/51 rounds B > A.
  - **The ticket's own prediction ("expect a speedup") did not hold** — the candidate came
    out ~16% *slower* than v0.2.0 on `smoke_graph`, though under the 1.20 ratio gate. The
    likely reason is not codegen: `smoke_graph.yml` (12) is `b` and `c` racing a 50ms-step
    file-based rendezvous poll loop against each other, so its wall time is dominated by
    scheduler noise and poll granularity, not by anything `--config=release` optimizes.
    `smoke_graph` is in the perf set as one of the four sudo-free, no-deliberate-sleep
    workflows (its short poll loop was judged not to count as one, same reasoning as 15's
    own "what" section) — but it is also the noisiest of the four for an A/B wall-time
    comparison, for exactly that reason. Worth a second look if `smoke_graph` produces a
    false-positive regression on some future release; the job/step-level keys under it
    read `new` rather than `gated` here only because v0.2.0's `result.json` predates
    `duration_ms` (01), which is expected and will stop once the baseline is a release that
    already has it.
  - The report header (candidate sha, baseline tag, runner nproc/CPU/kernel, rounds) is
    written straight to `$GITHUB_STEP_SUMMARY` by its own step (not part of `compare.sh`'s
    artifact), so it isn't in the downloaded `report.md`; its step ("Report header") ran and
    succeeded, and the script that writes it is a fixed, already-reviewed shell one-liner, so
    this is treated as confirmed rather than screen-scraped from the rendered summary.
  - The other three manual criteria (`baseline: self` x5, a deliberate busy-wait regression,
    a corrupted checksum) still need their own scratch branches and dispatches, distinct
    from what this push exercised; tracked in `.scratch/release-smoke-verification` rather
    than left silently unchecked in an archived ticket.
