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
- [ ] `baseline: self` dispatched 5 times → no regression in any run: `manual: gh workflow run smoke.yml -f baseline=self, x5; record the run URLs` (archived ticket 15)
- [ ] A scratch branch that adds a 5 ms busy-wait to the `run` plugin's path → the perf job fails and names the affected keys: `manual: workflow_dispatch on a scratch branch; record the run URL; do not merge` (archived ticket 15) — note the `smoke_graph` finding in archived ticket 15's comments before picking a key to check: its wall time is noisy (a file-poll rendezvous loop), so prefer a `smoke_run` or `smoke_file` step for a clean signal
- [ ] A checksum mismatch in the baseline download fails the job before any run: `manual: scratch branch with a corrupted expected sum in the download step; workflow_dispatch; record the run URL; do not merge` (archived ticket 15)
- [ ] A pre-release tag (`v0.3.0-rc1` after a version bump) runs `smoke` and `perf` with the right baseline (not itself) and publishes: `manual: bump QWE_VERSION; push the tag; record the run URL and the baseline tag shown in the summary` (archived ticket 16) — a real tag and release, not a scratch branch; confirm with the user first
- [ ] A smoke failure in a release turns a web-UI release back into a draft: `manual: covered by the draft-on-failure path; verify once on the rc above` (archived ticket 16) — same caveat

## Comments
