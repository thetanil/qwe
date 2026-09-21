# 08: The fuzz workflow, started by hand

Status: in-progress (manual criteria await a dispatch)
Category: enhancement
Type: task
Blocked by: 01

## What

`fuzz.yml`, whose only trigger is `workflow_dispatch`. It never runs on a push, a
tag or a schedule, and the release does not call it.

- Input `seconds`, defaulting to 3600. GitHub-hosted jobs stop at 6 h, so reject
  anything over 19800 (5.5 h) in a first step, which leaves room to build and
  upload. Set `timeout-minutes` from it.
- Setup: `apt: clang` (check that the image's clang has libFuzzer, and
  `--config=fuzz` sets `CC=clang`), `cache-key: fuzz`. No ssh target is needed.
- Run `tools/fuzz/nightly.sh ${{ inputs.seconds }}` with
  `QWE_FUZZ_DIR=${{ github.workspace }}/.fuzz`. It runs four processes: two
  targets, each under asan and ubsan. A standard runner has 4 vCPUs, which is
  what `docs/ci-checks.md` asks for, so check `nproc` in the log.
- **Persistent corpus.** Restore `$QWE_FUZZ_DIR` with `actions/cache/restore`,
  prefix key `fuzz-corpus-`. Save it with `actions/cache/save` under
  `fuzz-corpus-${{ github.run_id }}`, with `if: always()`, so a run that finds a
  crash still keeps what it learned. Caches are immutable per key, which is why
  the key is unique per run. Exclude `*-crashes/` from the saved cache, so a
  crash is reported once and then lives in the regression corpus, not in the
  cache.
- **Artifacts**, uploaded with `if: always()`: `$QWE_FUZZ_DIR/*-crashes/` and
  `*.log`. The job fails when `nightly.sh` exits non-zero, which it does when any
  crash file exists.
- Job summary: for each of the four processes, the executions, corpus size and
  coverage from the end of its `.log`. `docs/fuzzing.md` says the first scheduled
  runs should record these, so the first real run's numbers go there.

Add the fuzz badge. It shows the last manual run.

Update `docs/fuzzing.md`: the "Scheduled home" paragraph becomes "manual
dispatch of `fuzz.yml`", with how to start it
(`gh workflow run fuzz.yml -f seconds=3600`) and where the corpus and crashes
go. `nightly.sh` keeps its name, because renaming it is not worth the churn.

## Acceptance criteria

- [x] `workflows_test` accepts `fuzz.yml` without a push trigger, and fails if `fuzz.yml` gains one (push, schedule or `workflow_call`). `unit: tools/ci/workflows_test.sh::fuzz_manual_only`
- [x] `workflows_test` passes with `fuzz.yml` and its badge in place. `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] A push to `main` does not start `fuzz.yml`. `manual: push; check the Actions list`
- [ ] A dispatch with `seconds=300` builds, fuzzes, saves the corpus cache and finishes green, and the summary shows the four processes' numbers. `manual: gh workflow run fuzz.yml -f seconds=300`
- [ ] A second dispatch restores the first run's corpus, and its log shows the larger starting corpus. `manual: dispatch again; compare the "INITED" lines`
- [ ] `seconds=30000` is rejected before building. `manual: dispatch with it`
- [ ] A one-hour run's numbers are recorded in `docs/fuzzing.md`. `manual: gh workflow run fuzz.yml; copy from the summary`
- [ ] The fuzz badge renders. `manual: view README on github.com`

## Comments

- `timeout-minutes` is a constant 350: workflow expressions have no arithmetic, so it cannot be derived from `seconds`. The first step caps `seconds` at 19800, which keeps the run inside it.
- A step checks that clang links libFuzzer (`-fsanitize=fuzzer,address`) and prints `nproc`, so a runner image without either fails fast.
- The summary reads `stat::number_of_executed_units` and the last `#N DONE cov: … corp: …` line of each log; the format is unconfirmed until a real run.
- `workflows_test` rule 5 covers the push, schedule and workflow_call cases (`fuzz_manual_only`).
- Changed by the user after this ticket: `fuzz.yml` also has `workflow_call` (input `seconds`, default 3600) and `nightly.yml` calls it with `seconds: "3600"`. The release still does not. `workflows_test` rule 5 now allows `workflow_call`, still forbids `push` and `schedule`, requires the nightly to call `fuzz.yml` and the release not to.
- `actions/upload-artifact` moved from v4 to v7 (workflows and `collect-logs`).
