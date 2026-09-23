# The perf gate is A/B against the last release, not an absolute budget

`tools/perf/compare.sh` (14) decides whether a candidate build is slower than the last
release by running both, side by side, on the same runner, in the same job, and comparing
paired samples. It does not compare either one against a number written down in the repo.

Two absolute alternatives were rejected:

- **A budget file** (`smoke_run/wall: 250ms`, committed and hand-maintained). This needs
  someone to notice runner drift — a slower CPU generation, a noisier shared host, a Bazel
  or LuaJIT upgrade that shifts the baseline for everyone at once — and edit the number
  before it starts failing spuriously, or loosen it "temporarily" and forget. A budget also
  has no natural per-key granularity: either every workflow gets its own tuned number (a
  file that needs updating on every new smoke workflow) or one number covers unrelated
  keys badly.
- **Instruction counts** (`valgrind --tool=callgrind` or similar, comparing an instruction
  total instead of wall time). Deterministic and immune to runner noise, which is
  attractive, but it does not answer the question this gate exists to answer: whether a
  release actually got slower to use. An instruction count can rise with no wall-time cost
  (better locality, more cheaper instructions) or stay flat while wall time rises (a lock,
  a syscall, I/O). It also means running everything under an instrumented interpreter,
  which this project has no infrastructure for outside valgrind's own gate.

A/B against the release before it sidesteps both problems: the baseline moves with the
runner automatically (both binaries see the same CPU, the same day, the same noise), and
it measures the thing that matters (wall time, from `duration_ms` in 01) directly. The A
side is the last non-draft, non-prerelease GitHub release (15) — the most recent published
build a user could actually be running today — not a fixed historical point, so the gate
never asks "are we faster than v0.1.0 forever" and never needs updating by hand.

## The honest limit

This catches a **sustained regression of at least 20% and at least 500µs** (2000µs for a
workflow's wall time) on a key that ran in this release's perf set, at α = 0.01 per key
after a Bonferroni correction across the gated keys. It does not catch:

- **A slow accumulation.** Three releases in a row, each 8% slower than the last, compound
  to +26% but no single comparison ever crosses the 20% ratio gate. The gate resets its
  reference point every release; it was never designed to see across more than one.
- **Anything under the absolute floor**, even at a huge ratio. A step that goes from 50µs
  to 5000µs is a 100x ratio but clears 500µs by 4.5ms, so it would in fact regress -- the
  floor only matters near zero, where a ratio computed on noise-sized numbers is
  meaningless (30% of 50µs is 15µs, well inside round-to-round jitter).
- **A workflow or key outside the perf set** (10). Only `run`, `file`, `graph` and
  `project_plugin` run under this gate; `become` and `apt` need privileges a perf runner
  should not depend on, and are not measured at all.

## Consequences

- The gate is only as good as the runner it runs on: a genuinely noisy shared runner
  widens the sign test's practical detection floor (more paired rounds needed to reach
  significance at the same α), not the ratio/delta thresholds themselves.
- `baseline: self` (an A/A run of the same candidate against itself) is the calibration
  check for this: it should never regress, by construction, and a run that does points at
  the runner or the statistics, not the candidate.
- The allow list (14) is the one hand-maintained artifact this design still has, and it is
  deliberately narrow: a `<version> <key-glob> <max-ratio>` line only applies to its exact
  baseline version, so an accepted slowdown silently stops applying at the next release
  instead of quietly covering for a second one.
