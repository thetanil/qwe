# 01: The manual checks the release-smoke feature left open

Status: ready-for-human
Category: task
Type: task
Blocked by: none

## What

The `release-smoke` feature was archived (`.scratch/archive/release-smoke/`) with its automated
criteria passing and these manual ones not yet checked. Archived tickets are not edited, so they
are tracked here, the same way `.scratch/ci-verification` tracks what `ci` left open. Each needs
a throwaway branch or a real tag/release, which is why an agent did not do them without asking
first; two of the four (tickets 05, 15) below can be done as ordinary scratch-branch
`workflow_dispatch` runs like archived ticket 11's criterion 3 was. The last two (ticket 16) push
a real pre-release tag and create a real GitHub Release — ask before doing those; they are not a
scratch-branch action.

## Acceptance criteria

- [ ] Making `neg_file_ensure_denied.yml` succeed (e.g. `chmod 755` the target directory) fails the job instead of passing: `manual: workflow_dispatch on a scratch branch; record the run URL; do not merge` (archived ticket 06)
- [ ] Deliberately breaking `smoke_run.yml` (e.g. `exit 1` in a step) on a branch dispatch fails the `smoke` step and the job: `manual: workflow_dispatch on a scratch branch; record the run URL; do not merge` (archived ticket 05)
- [x] `baseline: self` dispatched 5 times → no regression in any run: `manual: gh workflow run smoke.yml -f baseline=self, x5; record the run URLs` (archived ticket 15)
- [ ] A scratch branch that adds a 5 ms busy-wait to the `run` plugin's path → the perf job fails and names the affected keys: `manual: workflow_dispatch on a scratch branch; record the run URL; do not merge` (archived ticket 15) — note the `smoke_graph` finding in archived ticket 15's comments before picking a key to check: its wall time is noisy (a file-poll rendezvous loop), so prefer a `smoke_run` or `smoke_file` step for a clean signal
- [ ] A checksum mismatch in the baseline download fails the job before any run: `manual: scratch branch with a corrupted expected sum in the download step; workflow_dispatch; record the run URL; do not merge` (archived ticket 15)
- [ ] A pre-release tag (`v0.3.0-rc1` after a version bump) runs `smoke` and `perf` with the right baseline (not itself) and publishes: `manual: bump QWE_VERSION; push the tag; record the run URL and the baseline tag shown in the summary` (archived ticket 16) — a real tag and release, not a scratch branch; confirm with the user first
- [ ] A smoke failure in a release turns a web-UI release back into a draft: `manual: covered by the draft-on-failure path; verify once on the rc above` (archived ticket 16) — same caveat

## Comments

2026-09-27, checked with `gh` against existing and newly dispatched runs:

**`baseline: self` x5: done.** Five `gh workflow run smoke.yml --ref main -f
baseline=self` dispatches, run one after another, because `smoke.yml`'s
concurrency group cancels an in-progress run on the same ref, so parallel
dispatches would cancel each other. Every run was `success`, and every perf
report says **PASS, no key regressed**, with all four workflows `gated` (not
`no-baseline`). Workflow-level ratios fell between 0.971 and 1.019. Each log
shows `BASELINE: self`.
- https://github.com/thetanil/qwe/actions/runs/36306496533
- https://github.com/thetanil/qwe/actions/runs/36306651801
- https://github.com/thetanil/qwe/actions/runs/36306769288
- https://github.com/thetanil/qwe/actions/runs/36306904110
- https://github.com/thetanil/qwe/actions/runs/36307057828

**Pre-release tag, right baseline, publishes: partly shown by the real v0.3.0
release, not ticked.** [Run 36186458092](https://github.com/thetanil/qwe/actions/runs/36186458092)
(tag push `v0.3.0`) passed `exclude-tag: v0.3.0` to smoke, and its perf job
compared against `v0.2.0` (`compare.sh "v0.2.0"`), not itself. It verified the
baseline's `SHA256SUMS` (`qwe-0.2.0-linux-x86_64: OK`), and `publish` succeeded.
Not exercised: the pre-release path (`case $TAG in *-*) pre=--prerelease`), and
whether "latest" skips a pre-release as a baseline. Both still need an `-rc` tag.

**Draft on failure: not shown.** In the v0.3.0 run, `draft-on-failure` was
`skipped` because nothing failed.

**Scratch-branch negatives (`neg_file_ensure_denied`, a broken `smoke_run`, the
5 ms busy-wait, a corrupted baseline sum): not done.** No existing run exercises
them. Each needs a scratch branch pushed. Agents push only ticket branches for a PR (`CLAUDE.md`), and these branches are deliberately red and never merged, so they wait for the user's say-so.
The failed smoke runs on `main` (35873743795, 35793715513, 36193715136) failed in
the build job, not in the smoke step, so they are not evidence for the
broken-`smoke_run` criterion.

**Finding: the v0.3.0 release's perf gate compared one workflow out of four.**
The `v0.2.0` baseline exited 2 on `smoke_run.yml`, `smoke_file.yml` and
`smoke_project_plugin.yml` (they use plugins and features added after v0.2.0).
`compare.sh` marked those `no-baseline` by design, and only `smoke_graph` was
gated (ratio 1.179, pass). That is the documented behaviour, but it means the
first release after new smoke workflows are added is barely perf-gated. Against
`v0.3.0` all four gate: today's `main` push, [run 36302821424](https://github.com/thetanil/qwe/actions/runs/36302821424),
had ratios 0.988 to 1.008, all `gated`, all pass.

2026-09-27, after `.scratch/perf-static-baseline` (ADR-0016): the perf job no longer
downloads a release, so **"a checksum mismatch in the baseline download"** has nothing left
to test and should close `wontfix`. **"The 5 ms busy-wait fails the perf job"** no longer
holds as written: since perf-static-baseline ticket 04 a key fails only above 2x its
expected median, and 5 ms turns `smoke_run/run` from about 9 ms into 14 ms (1.6x). Use a
busy-wait that at least doubles the step, e.g. 50 ms, on a scratch-branch dispatch
(`perf-gate` defaults to `true` there). The
`baseline: self` check above stays as evidence of the old A/B job's stability; there is no
`baseline` input any more.
