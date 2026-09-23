# Archived features

Closed features move here, one directory each, keeping their original layout
(`<feature>/spec.md`, `<feature>/issues/NN-<slug>.md`).

They are one level deeper than a live feature on purpose: the frontier scan
globs `.scratch/*/issues/*.md`, which does not reach `.scratch/archive/*/issues/`.
Nothing here is ever picked up as work, and nothing here should be edited — if
something in an archived ticket turns out to be wrong or unfinished, open a new
ticket in a live feature and say so there.

## Why these are kept rather than deleted

**Traceability.** Every acceptance criterion names the test that proves it
(`e2e: tests/e2e/<case>/`, `unit: <file>::<case>`). Across the archived
features that is a requirement-to-test map built while the code was written,
against tests that still exist. Reconstructing it later means re-deriving every
link by hand. A safety-certification effort asks for exactly this artifact.

**What was decided and why.** Each closed ticket's `## Comments` records what
the ticket left open and how it was settled, including the alternatives that
were rejected and the things found by a failing test rather than by design.

A decision that is still *load-bearing* does not belong here, though. Before a
feature is archived, its durable decisions are promoted to `docs/adr/`,
`CONTEXT.md` or the design docs, because those are what an agent reads.
An archived ticket is the record of how the project got somewhere, not the
statement of where it is.

## Archived so far

| Feature | Closed | What it built |
|---|---|---|
| `m1-engine` | 2026-09-20 | The M1 workflow engine: transcoder, schema validation, the job lifecycle table, steps, timeouts, cancellation, parallelism, plugins, check/apply, env and templating, the inventory, the ssh backend, `become`, secrets and redaction, `--job` selection. 21 tickets, 160 acceptance criteria. |
| `m1-review` | 2026-09-20 | A review of the finished M1 engine, and the fixes it found: duplicate YAML keys, `timeout-seconds` and a total conversion, the `$QWE_OUTPUT` file's mode, the ControlPath directory's owner, a master outliving a killed run, blocking ssh calls in the event loop, a dag check that approved when it could not run, the transcoder's trust in libyaml, the plugin trust boundary, disabled targets, and validation executing plugin code. 11 tickets, 74 acceptance criteria. |
| `quality` | 2026-09-20 | Making the C trustworthy: every exit path frees, every allocation is checked (`alloc.h`, and an audit of the bare calls), ASan/LSan, UBSan and Valgrind gates, coverage measurement for C and Lua with a ratchet, fuzzing the YAML edge, OOM injection over validate and run, and closing the C coverage gaps. 10 tickets, 64 acceptance criteria. |
| `release-smoke` | 2026-09-23 | Making a release trustworthy end to end: job/step `duration_ms`, the run summary and its hardening, the `--config=release` build, `smoke.yml`'s clean-runner smoke workflows and the negative-case pattern, four more built-in plugins (`file.read`, `assert`, `file.line`, `apt.package`) each with a smoke workflow and a negative, `become`/secrets smoke with real redaction verification, the job-graph/`--job`-selection smoke workflow, a coverage floor that guards every built-in plugin has a smoke workflow, an A/B perf gate (median/p90/ratio/delta floors, a Bonferroni-corrected paired sign test, a per-version allow list) and its `smoke.yml` job, `release.yml`/`nightly.yml` calling `smoke.yml`, PCRE2 for JSON Schema `pattern`/`patternProperties`, and graceful Lua panic handling. 18 tickets, 103 acceptance criteria (96 checked; the 7 manual ones left are tracked in `.scratch/release-smoke-verification`). |
| `ci` | 2026-09-21 | GitHub Actions for everything `quality` defined: a workflow and badge per check (tests, ASan, UBSan, Valgrind, coverage), an ssh target on the runner, a coverage report and percentage badge on Pages, a hand-started fuzz workflow with a persistent corpus, a nightly that refreshes the caches and runs all of it plus an hour of fuzzing, and a release built from a pushed tag after every gate reruns. 12 tickets, 65 acceptance criteria (45 checked; the 20 manual ones left are tracked in `.scratch/ci-verification`). |

What was promoted out of `quality` when it was archived: nothing new. Its decisions were written as each ticket closed, into `docs/ci-checks.md` (the commands and cadence CI should run), `docs/sanitizers.md`, `docs/valgrind.md`, `docs/fuzzing.md` and `docs/coverage.md` (how coverage is measured, and why each remaining miss is left), and the allocation policy in `src/kernel/alloc.h`. The pointer from `src/kernel/alloc_audit.txt` was repointed to the archived ticket.

What was promoted out of `m1-review` when it was archived: **ADR-0013** (a plugin
file has no top-level code, and validation reads it rather than running it — the
decision ticket 11 made, including the `kind:` attempt that was reverted); four
bullets in the qwe-ssh-sec Decision Log Addendum (pre-connect and the 100 ms
timeout guarantee with its 3 s reconnect exception, the `ControlPersist` ttl as a
backstop, the verified ControlPath directory, and the `0600` `$QWE_OUTPUT` file).
Most of this feature's decisions were already written into the docs as each
ticket closed — design §14 (the timeout unit, maximum and rounding), §15.4
(duplicate mapping keys), §13 and §7 (`disabled:` and `target-disabled`), §12
(plugin shape), qwe-ssh-sec I.2, I.3, I.5 and II.9, and `CONTEXT.md`'s **Strict
globals** and **Trust boundary** entries — which is why the promotion at archive
time was small. Two repairs went with it: `CONTEXT.md` still said that
*validating* someone else's workflow directory runs their code, which ticket 11
had made untrue, and ticket 05's edit to qwe-ssh-sec I.3 had swallowed the
opening of that section's second list item.

What was promoted out of `m1-engine` when it was archived: ADR-0011
(`become-denied` is detected by a probe), the session-cap rule and the
validation order in design §8.3 and §14, and the envelope text form, the
`connection-lost` definition, the failed-master-start behaviour, the secret tag
number and the Lua memory-zeroing limit in qwe-ssh-sec I.5, II.2 and II.4.

What was promoted out of `ci` when it was archived: nothing new. Its decisions were written as each ticket closed, into `docs/ci-checks.md` (the checks, their commands and cadence, the nightly and release flow, and "How CI reaches ssh"), `docs/fuzzing.md` (the fuzz workflow, its corpus cache and how to start it), and the README's CI section and "Making a release". `//tools/ci:workflows_test` enforces the rules the tickets set (a badge per workflow, every documented command in a workflow, the nightly and release calling every gate, `fuzz.yml` never on a push, the release never fuzzing). The feature was closed with its manual criteria unchecked, at the user's request: they are ticket 01 of `.scratch/ci-verification`, since an archived ticket is not edited. Ticket 11 says "CI run pending"; the `valgrind.yml` dispatch of 2026-09-21 (run 35663134677) passed all 213 tests, including `sites_oom_test`.

What was promoted out of `release-smoke` when it was archived: **ADR-0014** (the perf gate is
A/B against the last release, not an absolute budget or an instruction count, written as
ticket 14 closed); `docs/coverage.md` gained a toolchain-quirk note (ticket 09: a return-only
block nested inside `if/else` can show as an uncovered, unreachable line under
LuaJIT/luacov even when every branch is exercised — prefer a flat guard clause); and
`docs/ci-checks.md` gained a note on `workflows_test`'s rule 2 (ticket 10: it is a plain
substring match across every workflow file, so quoting a command verbatim in an unrelated
comment satisfies it without actually wiring anything up). Everything else was already
written into `docs/ci-checks.md`, `docs/smoke.md`, README and ADR-0005 (secrets travel apart
from logs, and need a declared `secret-outputs:` to join the redaction set — ticket 11's
negative control exercised that boundary for real rather than finding anything new). Most
manual criteria were closed for real once the user pushed (2026-09-23): the awk portability
bug that failed the first push is fixed in commit `c05921b`; ticket 11's leak-assertion
negative control was run on a throwaway `scratch/ticket-11-leak-check` branch and deleted
after ([run 35878451039](https://github.com/thetanil/qwe/actions/runs/35878451039)); the
rest were confirmed from [run 35865874382](https://github.com/thetanil/qwe/actions/runs/35865874382)
and [run 35875671339](https://github.com/thetanil/qwe/actions/runs/35875671339). Ticket 15's
comments also record the feature's first real A/B result: `smoke_graph` came out ~16%
*slower* than the v0.2.0 baseline (not the speedup the ticket predicted), most likely because
its rendezvous poll loop makes it the noisiest of the four perf-set workflows rather than
because of anything `--config=release` regressed. The 7 still-open manual criteria (a
scratch-branch negative for `smoke_run.yml` and for `neg_file_ensure_denied.yml`, `baseline:
self` x5, a deliberate perf regression, a corrupted baseline checksum, and the two that need
a real pre-release tag) are ticket 01 of `.scratch/release-smoke-verification`.
