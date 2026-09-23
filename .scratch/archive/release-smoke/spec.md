# Spec: release smoke workflow, verifying plugins, and a GitHub run summary

Status: ready-for-agent

## Problem Statement

Every quality gate runs qwe inside the Bazel sandbox, against goldens. Nobody has watched the binary
we release do real work on a clean Linux machine: install a package, edit a file, fail on a
permission error. When something goes wrong in a GitHub Actions job, qwe's output is terminal log
lines. The run summary page shows nothing about which qwe steps ran, which changed something,
which found nothing to do, which failed and why, or how long each took. There is also no signal at
all when a change makes qwe slower. Finally, qwe cannot check its own work: a workflow can change a
file, but no built-in step can read it back and assert on the result, so every check becomes an
ad-hoc `run:` shell line.

## Solution

A `smoke` GitHub workflow builds the release binary, with optimisation now on. It then runs qwe,
directly and step by step, on a set of smoke workflows that exercise every built-in plugin against
the runner machine itself (the `local` execution backend, no ssh).

- Each smoke workflow is one GitHub step. If qwe fails, the step fails, and so does the job.
- Each qwe run appends a clean markdown report to the GitHub run summary through a new
  `qwe run --summary <file>` option. The report shows every job and step with its outcome, reason,
  whether it changed anything, and its duration, plus a redacted log tail for anything that
  failed.
- Negative smoke cases are GitHub steps marked `continue-on-error`. A following step asserts that
  qwe failed, and that it failed with the expected, helpful message.
- New built-in plugins (`file.read`, `assert`, `file.line`, `apt.package`) let a smoke workflow do
  real work and then verify it with qwe steps: read, patch, read again, assert; install a package,
  confirm it, remove it, confirm again.
- A performance job compares the new binary A/B against the last release on the same runner. It
  appends a timing report to the summary, and fails on a statistically significant slowdown.
- The release workflow calls the smoke workflow as one more gate.

## User Stories

1. As a maintainer, I want a GitHub workflow that builds the release binary and runs our smoke workflows with it, so that I know the binary we ship actually works on a clean Linux machine.
2. As a maintainer, I want each smoke workflow to be its own GitHub step running `qwe run`, so that a failure points straight at the plugin area that broke.
3. As a maintainer, I want a failing qwe run to fail its GitHub step and job, so that a broken plugin cannot go green.
4. As a maintainer, I want a successful qwe run's results to appear in the workflow run summary, so that I can see what was checked without opening logs.
5. As a maintainer, I want the smoke workflow to run on every push to main, so that a regression shows up at the commit that caused it.
6. As a maintainer, I want the nightly workflow to call the smoke workflow, so that it also runs from cold caches.
7. As a maintainer, I want the release workflow to call the smoke workflow as a gate, so that a release whose binary fails smoke is turned back into a draft, like any other gate failure.
8. As a maintainer, I want the smoke workflow to have a README badge and a row in the CI docs, so that the drift test keeps holding.
9. As a maintainer, I want the smoke steps to run on a fresh runner with no Bazel and no ssh setup, so that the binary is proven standalone and passwordless sudo is still available for `become`.
10. As a maintainer, I want the smoke job to confirm the binary is statically linked, so that a linking regression is caught before release.
11. As a maintainer, I want the release binary built with optimisation on, and the full test suite run against that build, so that the shipped build is fast and still correct.
12. As an operator, I want `qwe run --summary <file>` to append a markdown report of the run to that file, so that I can point it at `$GITHUB_STEP_SUMMARY`.
13. As an operator, I want the summary to lead with the workflow file, the overall outcome and the total duration, so that the verdict is visible at a glance.
14. As an operator, I want a table of jobs with outcome, reason and duration, so that I can see which part of the graph failed.
15. As an operator, I want a table of steps for each job, with its id or name, the plugin it uses, outcome, changed or unchanged, reason and duration, so that I can see exactly what each step did.
16. As an operator, I want a step whose check found nothing to do shown as "unchanged", so that I can tell idempotent no-ops from real changes.
17. As an operator, I want a step that never ran shown as `skipped`, with its reason (for example `dependency-failed`), so that I can tell it apart from an unchanged step.
18. As an operator, I want durations in the summary with millisecond precision, so that I can see which steps are slow.
19. As an operator, I want a failed step to carry a collapsed tail of its job's log in the summary, so that I can read the error without opening the raw log.
20. As an operator, I want secrets redacted in the summary exactly as in the logs, so that a GitHub page never leaks a secret.
21. As an operator, I want the summary to render cleanly whatever the step output contains (pipes, backticks, newlines, very long lines), so that the report is never garbled.
22. As an operator, I want the summary written even when the run fails, times out or is cancelled, so that the failure is the case I can best see.
23. As an operator, I want qwe to say clearly when it cannot write the summary file, without changing the run's own exit status, so that a summary problem neither hides nor fakes a result.
24. As an operator, I want repeated `--summary` runs to append, not overwrite, so that several qwe steps in one GitHub step all show up.
25. As an operator, I want `result.json` to carry millisecond durations for each job and step, so that tools and the performance job can use the same numbers as the summary.
26. As a workflow author, I want a `file.read` step that reads a file on the target and outputs whether it exists, its content, sha256, mode and owner, so that later steps can check what earlier steps did.
27. As a workflow author, I want `file.read` to fail with a message naming the path and the OS reason when the file cannot be read, so that a permission problem is obvious.
28. As a workflow author, I want an `assert` step that compares an actual value with an expected one (equals, contains, matches a pattern), so that I can check outputs of earlier steps in the workflow itself.
29. As a workflow author, I want a failed `assert` to fail its step with a message showing expected and actual (redacted if secret), so that I know what went wrong.
30. As a workflow author, I want `assert` to run no command on the target, so that verifying costs nothing and changes nothing.
31. As a workflow author, I want a `file.line` step that makes sure a line is present or absent, or replaces a pattern, idempotently, so that I can patch config files and a second run changes nothing.
32. As a workflow author, I want `file.line` to report changed only when it edited the file, so that the summary shows real edits.
33. As a workflow author, I want an `apt.package` step with state present or absent, so that I can install or remove a package idempotently.
34. As a workflow author, I want `apt.package` to check the installed state with `apt list --installed` before doing anything, so that it runs apt-get only when the package is not already in the wanted state.
35. As a workflow author, I want `apt.package` to confirm the state again after apt-get, so that an install that silently failed is reported as `not-converged`.
36. As a workflow author, I want `apt.package` to fail with a helpful message for a package that does not exist or when not run as root, so that I know to fix the name or add `become`.
37. As a maintainer, I want a smoke workflow that does read, patch, read, assert on a file, so that `file.ensure`, `file.line`, `file.read` and `assert` are proven together on a real filesystem.
38. As a maintainer, I want a smoke workflow that checks a package with `apt list`, removes it if present, installs it, confirms it, installs it again (unchanged), removes it and confirms it is gone, so that both branches of `apt.package` run for real.
39. As a maintainer, I want a smoke workflow for `run`: outputs, env precedence and a multi-line script, so that the default step kind is proven.
40. As a maintainer, I want a smoke workflow for `become` on the local backend (root and a named user), so that privilege handling is proven.
41. As a maintainer, I want a smoke workflow for secrets: keygen, encrypt, use, redaction, so that the secret path works end to end in the shipped binary.
42. As a maintainer, I want a smoke workflow for the job graph (needs, max-parallel with a rendezvous, job-scoped outputs, `--job` selection), so that the scheduler is proven.
43. As a maintainer, I want a smoke workflow with a project plugin, so that loading a plugin from `.qwe/plugins/` works in the shipped binary.
44. As a maintainer, I want `qwe validate` run on every smoke workflow before it runs, so that a schema regression is caught separately from a runtime one.
45. As a maintainer, I want a negative case in which `file.ensure` writes into a directory it may not write to, and fails with a message naming the plugin, the path and "Permission denied", so that I/O errors stay helpful.
46. As a maintainer, I want a negative case in which `file.read` reads a file it may not read and fails with the same kind of message.
47. As a maintainer, I want a negative case in which `file.line` edits a read-only file and fails helpfully.
48. As a maintainer, I want a negative case in which `assert` fails with expected and actual in its message.
49. As a maintainer, I want a negative case for `apt.package` with a package name that does not exist.
50. As a maintainer, I want a negative case in which a step times out and fails with reason `timeout`, well within the expected time.
51. As a maintainer, I want a negative case in which a failed job skips its dependants with `dependency-failed`, and the summary shows them as skipped.
52. As a maintainer, I want a negative case in which a plugin with top-level code is refused by `qwe validate` with `file:line:col`.
53. As a maintainer, I want a negative case in which a malformed workflow is refused by `qwe validate` with a message naming the problem.
54. As a maintainer, I want each negative case to be a GitHub step with `continue-on-error: true`, followed by a step that fails the job unless the negative step's outcome was `failure` and its output contains the expected message, so that "fails when it should" is itself enforced.
55. As a maintainer, I want I/O error messages from the file plugins to be one clean line (plugin, operation, path, OS reason), without shell noise like `sh: 1: cannot create`, so that the summary reads well.
56. As a maintainer, I want a performance job that runs the new binary and the last release's binary alternately on the same runner, so that runner noise cancels out.
57. As a maintainer, I want the performance report appended to the run summary, with per-step medians, the change and a verdict, so that I can see how long each step took and whether it got slower.
58. As a maintainer, I want the performance job to fail only on a slowdown that is large, absolute and statistically significant, so that noise never fails the build.
59. As a maintainer, I want a way to accept a known, intended slowdown that expires at the next release, so that a costly feature can land without switching the gate off for good.
60. As a maintainer, I want the performance job to run report-only when there is no earlier release, or when the baseline cannot run a case, so that new cases and first releases work.
61. As a maintainer, I want to run the performance job against the candidate itself on demand (A/A), so that I can show the gate raises no false alarms.
62. As a maintainer, I want the timing samples kept as a workflow artifact, so that I can look at trends across releases.
63. As a plugin author, I want a documented checklist that every built-in plugin gets a smoke workflow, and a test that fails when one is missing, so that new plugins are smoke-tested from day one.
64. As a developer, I want the smoke workflows to also be valid qwe workflows I can run locally with the binary I built, so that I can reproduce a CI smoke failure without GitHub.

## Implementation Decisions

- **Release build mode.** A `release` Bazel config: optimised compilation plus debug info, so the debug binary keeps its symbols and the shipped binary is still stripped by the existing strip rule. Any warnings that optimisation exposes under `-Werror` in our own code are fixed, not suppressed. The release workflow's build and the smoke workflow's build both use this config. The full test suite also runs under it in the smoke workflow's build job. The source has no `assert()`, so `NDEBUG` changes no behaviour.
- **Smoke workflow shape** (reusable; triggers: push to main, manual dispatch, workflow_call):
  - *build*: shared setup action with the ssh target (the full suite needs it), the suite under the release config, then build and upload the binary as an artifact.
  - *smoke*: a fresh runner with no shared setup (so passwordless sudo remains and no Bazel exists). Download the binary, check it is statically linked. Then for each smoke workflow, one GitHub step runs `qwe validate` then `qwe run … --summary "$GITHUB_STEP_SUMMARY"`. Negative cases are `continue-on-error` steps, each followed by an assertion step that checks `steps.<id>.outcome == 'failure'` and greps the captured output for the expected message. There is no driver script: the GitHub workflow is the harness.
  - *perf*: its own runner. Fetch the last release's binary with its checksums verified, run the A/B, append the report to the summary, upload the samples.
- **Release integration.** `release.yml` calls the smoke workflow alongside the five existing gates. `publish` needs it, and draft-on-failure covers it. The publish job otherwise stays as today, apart from the release config. `nightly.yml` calls it too. The CI drift test gains a rule that both callers call it.
- **Smoke workflows** live together in one directory, named `smoke_<area>.yml`: `run`, `file` (ensure, line, read, assert, in a read, patch, read sequence), `apt`, `become`, `secrets`, `graph`, `project_plugin`, plus `neg_<case>.yml` for each negative case. Each one verifies itself with qwe steps (`file.read` + `assert`), falling back to `run:` only where no plugin fits. Steps that need a runtime value (the secrets blob, a read-only fixture dir) get it from a preceding step inside the same qwe workflow, or from a preceding GitHub step.
- **`qwe run --summary <file>`.** A new option on `run`, written once at the end of the run, next to `result.json`, from the same result data, so the two cannot disagree. It opens the file in append mode. Content:
  - a level-3 heading with the workflow file name, the overall outcome, and the total duration;
  - a jobs table: job, outcome, reason, duration;
  - one steps table per job: index, id or name, plugin (`run` or the `uses:` value), outcome, changed/unchanged (blank when it did not succeed), reason, duration in ms;
  - for each failed or cancelled step, a collapsed `<details>` with the last lines of the job log (a fixed count), already redacted, inside a code fence longer than any backtick run in the text.
  Table cells escape `|` and fold newlines. Outcome markers are text-first (for example "✅ success"), so the table also reads as plain text. The summary passes through the same redaction as the logs. A write failure prints `qwe run: cannot write summary <file>: <reason>` to stderr and leaves the exit status unchanged.
- **Durations.** The per-job and per-step records gain a monotonic duration in milliseconds, kept next to the existing wall-clock start and end. `result.json` gains `duration_ms` fields. Existing fields are unchanged.
- **Vocabulary.** "Unchanged" is not an outcome: it is a `success` whose Changed flag is false (check found nothing to do, so apply did not run). `skipped` stays the outcome of a step that never ran. The summary shows both, distinctly. No new outcome or reason is added, except as a plugin error message.
- **New built-in step plugins.** Each is a normal plugin: schema plus Lua, registered in the embedded modules and the coverage tooling, and every command goes through the job's execution backend, so each works over ssh too.
  - `file.read`: `with: path`. It changes nothing (`check` returns false). Outputs: `exists`, `content`, `sha256`, `mode`, `owner`. A missing file is not an error (`exists=false`); an unreadable one is.
  - `assert`: `with: actual`, plus exactly one of `equals` / `contains` / `matches` (a Lua pattern), and an optional `message`. It runs no command. `check` does the comparison and raises a plugin error on a mismatch. The message shows expected and actual, and redaction applies as usual. It never reports changed.
  - `file.line`: `with: path`, `line`, `state: present|absent` (default present), optional `regexp` (a line to replace). Idempotent. Output: whether it edited. It writes content through stdin, like `file.ensure`.
  - `apt.package`: `with: name`, `state: present|absent`. `check` reads `apt list --installed <name>`. `apply` runs a non-interactive `apt-get install -y` or `apt-get remove -y`. Converging needs `become: true`, and the plugin does not escalate by itself. An unknown package, or a missing privilege, fails with a clear message.
- **I/O error messages.** The file plugins turn a failed command into one line: `<plugin>: cannot <op> <path>: <OS reason>`. The OS reason is taken from the command's stderr, with shell prefixes removed and whitespace trimmed. This applies to `file.ensure`, `file.read` and `file.line`.
- **Performance gate** (decided earlier with the user; ADR to be written): A/B against the newest non-draft, non-pre-release release other than the tag being released, alternating in ABBA order over a fixed number of rounds after a warm-up. Metrics per smoke workflow in the perf set: process wall time, and each job's and step's `duration_ms` from `result.json`. A key regresses only when the median ratio exceeds 1.20, the median delta exceeds an absolute floor (0.5 ms per step/job, 2 ms for the whole run), and a paired sign test is significant at 0.01, Bonferroni-corrected across keys. An allow list keyed by baseline version accepts known slowdowns until the next release. Keys that exist on one side only are reported, not gated. The statistics are in shell and awk (no Python), with their own tests.
- **Coverage of smoke cases.** A test reads the built-in plugin registry and fails if a step plugin, or the `local` backend, has no smoke workflow. Test-only and ssh backends are exempt.

## Testing Decisions

- A good test drives qwe from the outside (a workflow in, stdout/stderr/`result.json`/summary out) and asserts on what an operator sees, not on internal functions. Timing is never asserted in the Bazel suite.
- **Seam 1, e2e (primary).** New cases under the existing e2e harness with the real binary and goldens:
  - the summary: success, a failure with a log tail, skipped plus unchanged steps, a secret redacted, markdown-hostile output (pipes, backticks), append across two runs, an unwritable summary path;
  - each new plugin working on the real filesystem (`file.read`, `assert`, `file.line`);
  - each helpful I/O error message (permission denied on write, read and line edit);
  - `duration_ms` present.
  The harness's normalisation of times gets extended to `duration_ms` and to the summary's duration column, so goldens stay stable. Prior art: `file_ensure_idempotent`, `secret_redacted_env`, `step_timeout_fails`, `continue_on_error`.
- **Seam 2, plugin tests on the recording backend**, for what the sandbox cannot do for real: `apt.package` (present/absent × already there / not there, unknown package, apt-get failure, not converged) and the command shapes of the file plugins. Prior art: `plugins/builtin/file.ensure/test.lua` and the plugin test runner.
- **Seam 3, the GitHub smoke workflow.** Its acceptance criteria are `manual:` (push, then read the run summary), as the archived CI tickets did.
- The performance statistics get a shell test over synthetic samples, as `tools/coverage/badge_test.sh` does: no change passes; a consistent +30%/+2 ms fails; a large ratio with a tiny delta passes; a noisy median shift without sign-test significance passes; the allow list applies only for its baseline version; new/gone/no-baseline keys pass.
- The CI drift test gets fixture cases for the new rule (release and nightly call smoke).
- Coverage floor: the new plugins are measured like the existing built-ins, and the floor is ratcheted after their tests land.

## Out of Scope

- Re-downloading or re-testing the published release assets after publish.
- Reusing the smoke build artifact in `publish`. The publish job builds as today, with the release config.
- ssh targets in the smoke workflow, and ssh timing.
- Any plugin beyond the four named here (no general package manager abstraction, no `dnf`).
- Changing outcomes or reasons, or adding a "skipped because unchanged" outcome.
- Trend dashboards. The samples are kept as artifacts only.
- Anything instruction-count based (callgrind) for performance.

## Further Notes

- The first A/B compares the optimised build with `v0.2.0` (built at `-O0`), so it should show a speedup, not a regression.
- The smoke job must not use the shared setup's ssh-target step: that step removes passwordless sudo, which the `become` and `apt` smoke workflows need.
- The summary is the start of a wider logging improvement. Its format is expected to change, and the goldens make every change deliberate.
- Where a smoke check would need a value qwe steps cannot yet read (for example another step's failure message), the check stays a GitHub assertion step. Each such spot is a candidate for a later qwe feature, noted in the ticket that meets it.
