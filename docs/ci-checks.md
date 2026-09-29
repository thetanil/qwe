# CI checks

GitHub Actions on `thetanil/qwe` runs every check below. Each is its own workflow in
`.github/workflows/`, with its own README badge (a status badge is per workflow file).
Run everything from the repository root; Bazel 8.7.0; clang for the fuzzers. The
`//tools/ci:workflows_test` test fails when a command in "Every push" is in no workflow,
or a workflow has no badge. `.github/actions/setup` is the shared setup: the runner
(`ubuntu-24.04`), Bazel at `.bazelversion`, apt packages, a Bazel disk cache per config,
and an sshd on `172.18.0.1` for the ssh e2e cases (`QWE_E2E_REQUIRE_SSH=1` makes a
skipped one a failure).

That "command is in no workflow" check is a plain substring match across every file under
`.github/workflows/`, not a real wiring check tied to the workflow that is supposed to run
the command. Quoting a command from this file verbatim in an unrelated comment (for
example, mentioning `` `bazel test //...` `` in a `smoke.yml` comment while the command
itself belongs to `tests.yml`) satisfies the check even though nothing new is actually
wired up. Describe a command in prose instead of quoting it exactly when the quote would
land somewhere other than the workflow that runs it.

**Reading a failure.** Runners are thrown away, so bazel's "see <path>/test.log" leads nowhere.
On a failure, `.github/actions/collect-logs` (every gate job calls it) uploads the logs as an
artifact and then runs `.github/scripts/test-failures.sh`: for each failed test, an error
annotation on the run page with the end of its log, the end of its log in the job summary, and
a failing "Failed tests" step at the end of the job, which is what
`gh run view <id> --log-failed` prints. `codeql.yml` is GitHub's code scanning, not a gate:
nightly and release do not call it, and `workflows_test` requires only its badge.

| Check | Workflow | Command | Cadence | Cost | Fails when |
|---|---|---|---|---|---|
| Tests | `tests.yml` | `bazel test //...` | every push to main, every pull request | seconds, warm cache | any test fails |
| ASan + LSan | `asan.yml` | `bazel test --config=asan //...` | every push to main, every pull request | about the plain suite | a memory error or leak |
| UBSan | `ubsan.yml` | `bazel test --config=ubsan //...` | every push to main, every pull request | about the plain suite | undefined behaviour |
| Valgrind, unit tests | `valgrind.yml` (job `unit`) | `bazel test --config=valgrind //...` | its own nightly-matching schedule, release and manual | about 8 min locally (the three oom sweeps: `oom_test` 486 s); 23 min on a runner | any error, leak or unsuppressed report |
| Valgrind, e2e | `valgrind.yml` (job `e2e`) | `bazel test --config=valgrind //tests/e2e:valgrind_e2e` | its own nightly-matching schedule, release and manual | about 6 s locally, 2 min on a runner | the same, in qwe and its step children |
| Static analysis | `static-analysis.yml` | `tools/clang-tidy/run.sh` | every push to main, every pull request, nightly, release and manual | about the plain suite's build, plus one clang-tidy pass per (file, flag set) pair across the default, coverage, valgrind, sanitizer and fuzz configurations (186 pairs, about two and a half minutes with cross-translation-unit analysis), pinned to clang-tidy `20.1.8` and the `clang-extdef-mapping` of the same build (`tools/clang-tidy/pin.env`) | any `clang-analyzer-*`/bugprone/cert/concurrency/performance/portability finding in `src/`, `plugins/` or `tools/`, or a `.c` file there that no pair covers (`tools/clang-tidy/coverage_test.sh`) |
| Coverage | `coverage.yml` | `bazel run //tools/coverage:check` | every push to main, every pull request | about 35 s warm | any file under `src/`, `plugins/` or `tools/` has less than 85% of its lines covered |
| Smoke | `smoke.yml` (job `build`, then `smoke` and `perf`) | `./qwe run tests/smoke/smoke_run.yml --debug --summary "$GITHUB_STEP_SUMMARY"` | every push to main, every pull request | build: one `bazel build --config=release //src/cli:qwe`; smoke seconds on a fresh runner; perf about a minute (51 rounds of the candidate only) | smoke: the release binary fails a real smoke workflow or negative case, or is not statically linked (the `--debug` lifecycle traces are kept as the `smoke-runs` artifact). perf: a key regressed against `tools/perf/expected.tsv` (ADR-0016); this fails the job only when gating (`release.yml`, a manual dispatch), and otherwise is a warning with the report in the run summary |
| Fuzzing | `fuzz.yml` | `tools/fuzz/nightly.sh [seconds]` | its own nightly-matching schedule (3600 s) and manual | hours; four processes in parallel | any crash artifact exists |

Every gate above but valgrind also runs on `pull_request`, so a finding is checked before a
change merges, not only after it lands on `main`. Valgrind's 23 minutes is too slow to gate a
pull request on, so it stays nightly/release/manual only. `workflows_test` rule 3 enforces the
`pull_request` trigger on every gate but valgrind, not just the badge and the `workflow_call`.

Details for each live in `docs/sanitizers.md`, `docs/valgrind.md`,
`docs/static-analysis.md`, `docs/coverage.md` and `docs/fuzzing.md`. What
follows is only what CI needs.

## Every push

```
bazel test //...
bazel test --config=asan //...
bazel test --config=ubsan //...
tools/clang-tidy/run.sh
bazel run //tools/coverage:check
./qwe run tests/smoke/smoke_run.yml --debug --summary "$GITHUB_STEP_SUMMARY"
tools/perf/compare.sh tools/perf/expected.tsv perf-out/samples.tsv tools/perf/allow-list.txt perf-out
```

`check` runs `bazel coverage //... --combined_report=lcov` itself and fails if
any file under `src/`, `plugins/` or `tools/` has less than 85% of its lines covered,
listing the uncovered lines. For a browsable report, `bazel run //tools/coverage:html`
(needs `genhtml`, from the `lcov` package) writes `coverage-html/`; the coverage workflow uploads it as
an artifact. On a green push to main it is also deployed to GitHub Pages (`https://thetanil.com/qwe/`, Settings, Pages, source "GitHub Actions"), next to `coverage.json`, the percentage badge document that `tools/coverage/badge.sh` writes (line coverage of `src/` and `plugins/`; red under 70, yellow under 85, green above).

## Pull request gates

Every ticket is a branch and a pull request (`CLAUDE.md`, "Working rules"), so the checks that run
on `pull_request` are the gates a ticket has to pass. Merging waits for them; an agent stops at a
green PR and the user merges. The jobs to require, by the name a check shows on the PR:

| Job | Workflow |
| --- | --- |
| `tests` | `tests.yml` |
| `asan` | `asan.yml` |
| `ubsan` | `ubsan.yml` |
| `static-analysis` | `static-analysis.yml` |
| `coverage` | `coverage.yml` |
| `build`, `smoke` | `smoke.yml` |

Require `build` as well as `smoke`: `smoke` needs `build`, and a job skipped because its `needs`
failed counts as passing for a required check, so `smoke` alone would let a broken build through.
`perf` is not required: it reports on a pull request and only gates a release or a manual dispatch
(ADR-0016). `codeql.yml`'s `Analyze (c-cpp)` joins the list when `.scratch/sca-round3` ticket 26
has it building for real. Valgrind and fuzz never run on a pull request.

These are the repo's own commit checks (`bazel test //...` green before a commit, then CI's
sanitizers, clang-tidy, coverage floor and smoke on the PR). What GitHub enforces is the ruleset
named `PR` (Settings, Rules), which applies to the default branch and has no bypass actors:

- a change reaches `main` only by pull request, and only by squash merge, so a ticket lands as one commit;
- linear history is required; the branch cannot be deleted or force-pushed;
- no approving review is required (`required_approving_review_count` is 0), and Copilot code review runs on push.

The ruleset does **not** yet have a "Require status checks to pass" rule, so the jobs in the table
above are gates by this repo's rule (`CLAUDE.md`) and not yet by GitHub's: a red PR can be merged.
Add the rule to the `PR` ruleset with the seven job names above (`tests`, `asan`, `ubsan`,
`static-analysis`, `coverage`, `build`, `smoke`), and turn on "Require branches to be up to date
before merging" only if a stale base should block a merge too. Check the names against a real PR
first (`gh pr checks <n>`); a required name that no job reports blocks every merge. A new gate
workflow is added to the table and to that rule in the same PR.

## On demand

Valgrind takes 23 minutes on a runner, so it never runs on a push or a pull request. It runs from
`workflow_dispatch` (by hand), its own `schedule` (13 minutes after `nightly.yml`'s cron, past that
workflow's cache-clearing step -- see "Nightly and release" below for why it is not called from
there), and from `release.yml` (by `workflow_call`, gating the exact tagged commit).
`workflows_test` checks these commands too.
`release.yml` builds the shipped binaries with the release config (same codegen as fastbuild,
debug info kept for `qwe-debug`; the strip rule still strips `qwe`).

```
bazel build --config=release //src/cli:qwe //src/cli:qwe-debug
bazel test --config=valgrind //...
bazel test --config=valgrind //tests/e2e:valgrind_e2e
```

## Re-measuring the perf expected values

`smoke.yml`'s perf job compares the candidate with `tools/perf/expected.tsv`: per-key medians,
stored once, not re-measured on every run (ADR-0016). They are re-measured by
`perf-baseline.yml`, which starts only by hand (`workflows_test` rule 7):

```
gh workflow run perf-baseline.yml -f ref=main -f version=v0.4.0
```

It builds the release binary of `ref` once, times it for `rounds` (default 51) on five runners,
pools the samples with `tools/perf/expect.sh`, and opens a pull request that replaces
`expected.tsv`. The PR description lists the five runners' CPU models and every median's old and new
value. Merge it like any other change. Doing so expires the `tools/perf/allow-list.txt` lines for
the old version.

Run it when code got faster or slower on purpose, when the report on pushes to `main` has been
drifting for no code reason (GitHub moved its runners to other CPUs), or before a release whose
perf gate would otherwise fail on a change you accept. The PR needs the repo setting "Allow GitHub
Actions to create and approve pull requests" (Settings, Actions, General). A new workflow in
`tools/perf/workflows.txt` needs a re-measure before it is gated: `//tools/perf:expected_test` fails
until `expected.tsv` has it.

## How CI reaches ssh

The 19 e2e cases that need ssh connect to `172.18.0.1`, the devcontainer's host, which is
hardcoded in their `inventory.yaml` files. `.github/actions/setup` with `ssh-target: "true"`
(tests, asan, ubsan, valgrind, coverage) makes a runner look like that host:

- **The address.** `172.18.0.1/32` is added to `lo`, so it is reachable and needs no network.
- **sshd and a key.** sshd runs on the runner; a fresh ed25519 key is authorised for the runner
  user and held by an `ssh-agent` at `~/.ssh/agent.sock` (the cases authenticate through
  `SSH_AUTH_SOCK`). `known_hosts` is filled with `ssh-keyscan`, since the cases run with
  `BatchMode=yes` and cannot answer a host-key prompt.
- **No passwordless sudo.** `ssh_become_denied` needs a target user that cannot `sudo`, as on zeta.
  The runner user has a `NOPASSWD` rule, so the setup removes whichever files in `/etc/sudoers.d`
  grant it, then fails the job if `sudo -n true` still works, locally or over ssh. This is the last
  thing the step does: **no later step of the job may use `sudo`.**
- **Environment.** `REMOTE_CONTAINERS=1` and `QWE_E2E_REQUIRE_SSH=1` are written to `$GITHUB_ENV`,
  and `SSH_AUTH_SOCK` likewise. `~/.bazelrc` belongs to `setup-bazel`, so nothing goes there. The
  repo `.bazelrc` has `test --test_env=<NAME>` for each, which forwards the value from the
  environment to the tests; there is no second copy that could win. With `QWE_E2E_REQUIRE_SSH=1`
  an unreachable target fails the case instead of skipping it.
- **The agent's life.** The agent is started with `RUNNER_TRACKING_ID=""` so the runner does not
  kill it when the step ends. Every job gets its own VM, so the two `valgrind.yml` jobs each start
  their own sshd and agent and nothing outlives the job.
- **Fallback.** None is coded: the address is in the case files, and there is no host override
  variable. If the loopback alias ever stops working on runners, the alternatives are a different
  local alias in both the action and the inventories, or running the ssh cases only where a real
  target exists.

## Nightly and release

`nightly.yml` runs at 02:17 UTC and on demand. It first deletes every `setup-bazel-*` cache (a saved
cache key is never rewritten, so the gates' caches go stale), then calls its five gate workflows:
tests, asan, ubsan, static-analysis and coverage, and `smoke.yml` (build, smoke, and perf reporting against
the stored expected values without gating). They run cold and save fresh caches, which pushes to main then
restore. `valgrind.yml` and `fuzz.yml` are deliberately not called from here any more -- each has its own
`schedule` a few minutes after this file's cron instead (see "On demand" above and "Fuzzing" below), so
its own status badge reflects a real nightly run: a `workflow_call` from a job in this file never updates
the called workflow's own badge, only a direct trigger does. `refresh-caches` still clears their disk
caches too, since it deletes every `setup-bazel-*` entry repo-wide, not just the ones this file's job
graph happens to use. `workflows_test` fails if `nightly.yml` stops calling one of its five gates or
`smoke.yml`, or starts calling `valgrind.yml` or `fuzz.yml` directly again.

`release.yml` runs on a pushed tag `v*`. Write the release in the GitHub web UI (its notes, and the tag it
creates on publish), or push the tag yourself. All six gates -- `nightly.yml`'s five (tests, asan, ubsan,
static-analysis, coverage) plus `valgrind`, which `release.yml` calls directly -- and `smoke.yml` run
again at the tagged commit (not the fuzzer, which is never part of a release); `smoke.yml` is called
with `perf-gate: true`, so a perf regression against
`tools/perf/expected.tsv` fails the release (on a push and nightly it only reports). Then `qwe` and `qwe-debug` are built,
`tools/release/check_version.sh` checks that the tag, `QWE_VERSION` in `src/kernel/qwe.h` and
`qwe --version` agree, and the two binaries, `SHA256SUMS` and the static-analysis evidence bundle
(`clang-tidy-evidence-<version>.zip`, downloaded from the `static-analysis` job's own artifact,
which expires in at most 90 days — see `docs/static-analysis.md`; a release asset does not)
are attached to the release. The full
commit hash is appended to the release notes (a release created by the workflow is titled
`<tag> (<short sha>)`). A release made in the web UI is public while the gates run; if a gate (smoke
included) or the version check fails, the workflow turns it back into a draft. A pre-release tag
(`v0.2.0-rc1`) is published as a pre-release. Bump `QWE_VERSION` first, or the version check fails.

MSan is not supported (`docs/sanitizers.md` says why); do not add a job for it.
The `sanitizer_smoke_*` tests each config builds fault on purpose and pass only
if the sanitizer stops them, so a config that silently lost its flags fails
loudly rather than passing green.

## Fuzzing

`tools/fuzz/nightly.sh` builds `//src/edge/yaml:transcode_fuzz` and
`//src/edge/yaml:chain_fuzz` under `--config=fuzz` with `--config=asan` and
`--config=ubsan`, then runs all four for `seconds` (default 3600) in parallel.
It needs clang (`--config=fuzz` sets `CC=clang`) and 4+ idle cores.

- **Persistent corpus.** `$QWE_FUZZ_DIR` (default `~/.cache/qwe-fuzz`) holds
  `<target>-<san>/` corpora, `<target>-<san>.log` and `<target>-<san>-crashes/`.
  The job must cache or mount that directory between runs, or every night starts
  from the seeds again. Seeds come from `tests/e2e/*/w.yaml`, `inventory.yaml` and
  `src/edge/yaml/corpus/`, and the dictionary is `src/edge/yaml/qwe.dict`.
- **Exit status.** Non-zero if any file exists under a `*-crashes/` directory.
  Upload `$QWE_FUZZ_DIR/*-crashes/` and the `.log` files as job artifacts.
- **A crash becomes a test.** Once understood and fixed, copy the file into
  `src/edge/yaml/corpus/`. `//src/edge/yaml:corpus_test` (in `bazel test //...`,
  and under asan and ubsan) replays that directory and every e2e workflow through
  both entry points, so the fix stays fixed. Clear the crash artifact afterwards.
- **Scope.** Only `src/` is instrumented; libyaml and LuaJIT are not fuzzed
  (open question 10 in `workflow-kernel-design.md`).
- **Not yet recorded.** The ticket's one-hour run (iterations, corpus size,
  coverage) was dropped in favour of CI. The first scheduled runs should record
  them here.

Fuzz binaries are `manual`-tagged, so `bazel test //...` and `bazel build //...`
never build them.

## Allocation checks

No separate command: all of these are ordinary tests in `bazel test //...`, and so
also run under the asan, ubsan and valgrind configs above.

- **OOM injection** (`quality/07`, `08`): `oom_test` in `src/kernel`, `src/cli/validate`,
  `src/secrets`, `src/cli/run` and `src/cli/encrypt`, plus `src/kernel:sites_oom_test` and
  `load_oom_test`. Each fails the nth allocation for every n and fails on a fault, a
  hang, or a silently short success. Under valgrind they are the slow part.
- **Bare-call audit**: `//src/kernel:alloc_audit_test` compares the count of bare
  `malloc`/`calloc`/`realloc`/`strdup`/`strndup` per file with `src/kernel/alloc_audit.txt`.
  It fails when a call appears or goes; read the new call against `src/kernel/alloc.h`,
  then update the list. `tools/bcembed.c` is exempt (build-time tool).
- **Which allocations a test fails**: run the test with
  `QWE_OOM_SITE_LOG=<file>` (`--test_env`, and `--copt=-g --strip=never`), then
  `addr2line -i -e <test binary> $(sed 's/^/0x/' <file>)`. Coverage shows which lines
  the sweeps reach (the probe child dumps its gcov data before `_exit`; not in
  `//src/kernel:oom_test`, which unsets `QWE_LUA_COVERAGE`), not which injection
  reached them.
