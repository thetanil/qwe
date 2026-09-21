# 05: The valgrind workflow

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: 02

## What

`valgrind.yml`, on the same triggers as the other gates, with setup
`apt: valgrind`, `cache-key: valgrind` and `ssh-target: true`. It is one workflow
with one badge, and it runs two jobs in parallel:

- `unit`: `bazel test --config=valgrind //...`. This took about 130 s in the
  devcontainer, most of it `load_oom_test`. A 2-core runner will be slower, so
  measure it. `.bazelrc` sets `--test_timeout=300`. If `load_oom_test` gets close
  to that on the runner, record the time and raise the timeout for that test
  only. `docs/valgrind.md` already names the escape hatch (excluding it), but
  use that only with the user's agreement.
- `e2e`: `bazel test //tests/e2e:valgrind_e2e`.

`docs/valgrind.md` notes that 3.22 was measured, and it names "Not in CI yet:
`--config=valgrind` needs `valgrind` installed". Check `valgrind --version` in
the log. If the runner's version differs, record it under Comments.

On failure, upload the per-pid valgrind reports from the test outputs
(`valgrind-<case>/valgrind.<pid>` for e2e, and the `run_under.sh` logs for unit
tests).

`//src/kernel:valgrind_smoke_test` proves that the gate is live. It must run and
pass in the `unit` job.

Add the valgrind badge.

## Acceptance criteria

- [x] `workflows_test` passes with `valgrind.yml` and its badge in place. `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [ ] A push to `main` runs both jobs green, and `valgrind_smoke_test` ran in `unit`. `manual: push; read both job logs`
- [ ] The run's timing is recorded in `docs/valgrind.md`'s timing table as a "GitHub runner" row. `manual: copy from the run`
- [ ] A deliberate uninitialised read turns `unit` red, and the artifact holds the valgrind report. `manual: throwaway branch, as in ticket 03`
- [ ] The valgrind badge renders. `manual: view README on github.com`

## Comments

- valgrind.yml added with badge: jobs unit and e2e in parallel, each printing valgrind --version. Timing row for docs/valgrind.md and the runner's version are to be copied from the first run. valgrind_smoke_test is an ordinary cc_test, so it runs in unit.

- First run (19fb71c, run 35623889413), both jobs red, not fixed here because each needs a decision:
  - `e2e`: all six `*_valgrind_test` report `Syscall param set_robust_list(head) points to uninitialised byte(s)` (address in the brk data segment) in the static `qwe`. Runner has valgrind 3.22.0 and glibc 2.39. Cause unverified; a suppression would change the gate.
  - `unit`: `//src/cli/validate:oom_test` hit the 300 s `--test_timeout` (the runner is slower than the 16-core devcontainer), and `//src/kernel:sites_oom_test` reported `4 bytes ... definitely lost` via realloc. Not yet looked at.
- Investigated, all four failures reproduced locally in the devcontainer (the gate had drifted since `quality/04`):
  - `e2e`: the command lacked `--config=valgrind`. The shipping `qwe` is static (`src/cli/BUILD`), and valgrind cannot intercept malloc in a static binary, so glibc's startup reports `set_robust_list` and uninitialised jumps in `malloc`. With `--config=valgrind` the dynamic build is used and all six pass. Command changed in `docs/ci-checks.md`, `docs/valgrind.md`, `valgrind.yml`.
  - `unit`, `corpus_test`: LuaJIT's `__register_frame` (unwind info of its mcode areas) is reported definitely lost once traces compile. One suppression added to `luajit.supp`, commented.
  - `unit`, `sites_oom_test`: see ticket 11 (the test's own leak, fixed).
  - `unit`, timeouts: `src/kernel:oom_test` takes 486 s, `src/cli/validate:oom_test` 382 s and `load_oom_test` 133 s in the devcontainer (16 cores), so they exceed the 300 s limit. Not excluded: the three are `timeout = "eternal"` and `.bazelrc` gives eternal 3600 s under `--config=valgrind`. `bazel test --config=valgrind //...` is green locally in 8 min. The runner has fewer cores: its timing is the next thing to record.
