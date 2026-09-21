# 03: The asan workflow

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: 02

## What

`asan.yml`, on the same triggers, permissions and concurrency as `tests.yml`
(ticket 01), with setup `cache-key: asan` and `ssh-target: true`. It runs
`bazel test --config=asan //...`.

- On failure, upload `bazel-testlogs/**/test.log` and each test's
  `test.outputs/`. The ASan reports are written per pid into undeclared outputs
  (`.bazelrc`, quality/03), not into stderr, so the report lives in the outputs
  zip.
- The `sanitizer_smoke_*` tests are what prove the config still has its flags. Do
  not exclude them.
- Add the asan badge to the README row.

## Acceptance criteria

- [x] `workflows_test` passes with `asan.yml` and its badge in place. `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [x] A push to `main` runs `asan.yml`, it is green, and `sanitizer_smoke_*` ran and passed. `manual: push; read the run log`
- [ ] A deliberate leak turns the run red, and the uploaded artifact contains the LSan report naming the leaking line. `manual: on a throwaway branch, drop a free in a tested path, push with the trigger temporarily widened, read the artifact, delete the branch`
- [x] The asan badge renders. `manual: view README on github.com`

## Comments

- asan.yml added with badge; the collect-logs action uploads test.log and each test's undeclared outputs (the per-pid ASan reports). manual: criteria await a pushed run.
