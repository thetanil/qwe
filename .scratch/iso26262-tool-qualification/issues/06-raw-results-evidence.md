# 06: Raw test results carried unchanged, with their hash

Status: needs-triage
Category: enhancement
Type: task
Blocked by: 01

## What

`run:` steps are outside qwe's safety-rated scope (ADR-0015). The user takes
responsibility for them, and the current user does so with an independent
downstream gate (A1) and by treating the raw results in their long-term archive
as the determinative record (A2). This ticket is **not** part of the
qualification. It is a product feature that makes A2 easy to meet: the archive
should get the raw results intact, and be able to show they are intact. Today a
step's outcome comes from its exit status, and whatever the test framework
wrote on the DuT (JUnit XML, a TAP stream, a log) is at best in the step's
output. Nothing
ties the report to that file, or shows that it arrived intact.

The false passes this is aimed at: an exit status lost or misread, output
truncated, a timeout or crash read as success, the wrong DuT.

## Fix

- A step can declare the result files its test framework writes on the DuT.
  qwe fetches each one **unchanged** into the run directory and records its
  SHA-256, its size and the DuT's identity (hostname and `boot_id`, and the booted
  image identity from ticket 05 where there is one) in `result.json`.
- A declared result file that is missing, empty or unreadable makes the step
  **fail**, whatever its exit status was. A declared file that says tests failed
  while the exit status says success also fails the step, with a reason naming
  the disagreement. (qwe reads only as much of the format as it needs for this:
  a failure count. It is not a report generator.)
- The summary says, for each step with declared results, where the raw files are
  and their hashes, and that they, not the summary, are the evidence.

Which formats to understand for the failure count (JUnit XML and TAP to start
with?) is decided at triage.

## Acceptance criteria

- [ ] The tool safety manual's "outside the claim" section points to this
      feature as a help for A2, and does not list it as a tool requirement.
      `manual: docs/tool-safety-manual.md`
- [ ] A declared result file is fetched byte-for-byte and its hash is in
      `result.json`. `e2e: tests/e2e/<case>/`
- [ ] Missing file, empty file, and "failures > 0 but exit 0" each end with the
      step failed and a reason naming the check. `e2e: tests/e2e/<cases>/`
- [ ] `bazel test //...` is green.

## Comments
