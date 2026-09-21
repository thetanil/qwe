# 12: The nightly full run, which refreshes the caches

Status: in-progress (manual criteria await a run)
Category: enhancement
Type: task
Blocked by: 06

## What

`nightly.yml`, on a schedule (02:17 UTC) and `workflow_dispatch`. It deletes every `setup-bazel-*`
Actions cache (a saved key is never rewritten, so the gates' caches go stale), then calls the five gate
workflows (tests, asan, ubsan, valgrind, coverage) with `uses:`. They run cold and save fresh caches.
The called gates' concurrency groups now start
with `github.workflow`, so a nightly run and a push do not cancel one another.

## Acceptance criteria

- [x] `workflows_test` fails when `nightly.yml` stops calling a gate. `unit: tools/ci/workflows_test.sh::nightly_calls_every_gate`
- [ ] A dispatched nightly runs all five gates green. `manual: gh workflow run nightly.yml`
- [ ] After it, pushes to main restore caches saved by the nightly (log: "Cache hit"). `manual: push after the run; read the log`
- [ ] The nightly badge renders. `manual: view README on github.com`

## Comments

- The nightly also calls `fuzz.yml` for 3600 s (see ticket 08). Not yet seen running.
