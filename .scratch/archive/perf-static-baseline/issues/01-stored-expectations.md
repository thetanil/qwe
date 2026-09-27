# 01: Stored expectations, and the tools to make and use them

Status: resolved
Category: enhancement
Type: task

## What

Replace the A/B comparison against a downloaded release with a comparison
against stored values.

- `tools/perf/expect.sh`: pools samples from one or more `samples.tsv` files
  (each with the bin labels to take) into an expected-values file: per key, the
  median, the p90 and the sample count, with a `# version:` header and comment
  lines saying where the samples came from.
- `tools/perf/expected.tsv`: the first expected values, from the v0.3.0 binary
  (the five runs in the spec).
- `tools/perf/run.sh`: times only the candidate. One discarded warm-up round,
  then `<rounds>` timed rounds.
- `tools/perf/compare.sh`: compares the candidate's samples with
  `expected.tsv`. The same thresholds as before (ratio > 1.20, delta above the
  500 µs step / 2 ms wall floor, a significant one-sided sign test with
  Bonferroni over the gated keys). The sign test becomes one-sample: rounds in
  which the candidate is above the stored median, ties dropped. A key only in
  the samples is `new`, a key only in the expected values is `gone`; neither is
  gated. The allow-list matches the expected file's version.
- ADR-0016, superseding ADR-0014.

## Acceptance criteria

- [x] `expect.sh` pools several files, takes only the named bins, and writes the
      median, the p90 and n per key, with the version header.
      `unit: tools/perf/expect_test.sh`
- [x] `compare.sh` against an expected file: no change passes; a +30%/+2 ms shift
      fails and names the key; a shift below the floor passes; a shift that is
      not significant passes; the allow-list matches the expected file's version
      only; `new` and `gone` keys are reported and not gated; the sign-test
      critical values for n = 11, 31, 51 hold. `unit: tools/perf/compare_test.sh`
- [x] `run.sh` discards the warm-up round, records only the candidate, and fails
      when the candidate fails. `unit: tools/perf/run_test.sh`
- [x] `tools/perf/expected.tsv` holds the v0.3.0 values for all 40 keys of the
      four perf workflows, with its sources listed. `unit: tools/perf/expected_test.sh`
- [x] ADR-0016 is written, and ADR-0014 says it is superseded.
      `manual: docs/adr/`
- [x] `bazel test //...` is green.

## Comments

2026-09-27: done.

- `expected.tsv` pools 255 rounds per key (40 keys) of the v0.3.0 binary from five runs:
  36186458092 (the release run, bins B and no-baseline), and 36194788784, 36196615241,
  36245001109, 36302821424 (main pushes, bin A). The CPU models of those runners were
  never recorded, and the file says so.
- **The step floor went from 500 µs to 2 ms.** Checked against six real candidate runs
  (today's five `baseline=self` dispatches, and push 36302821424), the first version failed
  four of them on step keys under `smoke_graph`, with nothing changed. The cause: step and
  job times are whole milliseconds (`duration_ms`), so a 2 ms step reads 3 ms on a slightly
  slower runner, a ratio of 1.5 and a delta of 1 ms. A/B hid this because both binaries
  shared the runner. At a 2 ms floor all six pass. Their wall ratios fell between 0.97 and
  1.06, and a 5 ms slowdown in a step still clears the floor. ADR-0016 records it.
- The tests moved their regression values above the new floor.
- So that this commit leaves CI coherent, `smoke.yml`'s perf job already calls the new
  `run.sh` and `compare.sh`: candidate only, against `expected.tsv`, still gating
  everywhere. Ticket 02 does the rest (build once, gate on release only, the inputs).
- `bazel test //...`: 262 pass, 3 skipped (the sanitizer and valgrind smoke tests, which
  run only under their own configs).
