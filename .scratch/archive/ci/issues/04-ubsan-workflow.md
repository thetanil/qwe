# 04: The ubsan workflow

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: 02

## What

`ubsan.yml` is the same as `asan.yml` (ticket 03), with `cache-key: ubsan`, and it
runs `bazel test --config=ubsan //...`. `docs/ci-checks.md` calls UBSan nightly.
This feature runs it on every push, and ticket 06 updates the doc.

UBSan is gcc's here, the same as in the devcontainer. `.bazelrc`'s `ubsan` config
passes `-fsanitize=float-cast-overflow` and `--per_file_copt` for third_party.
Confirm that the runner's gcc accepts both. If it does not, record it under
Comments. Do not quietly drop the flag.

Add the ubsan badge.

## Acceptance criteria

- [x] `workflows_test` passes with `ubsan.yml` and its badge in place. `unit: tools/ci/workflows_test.sh::repo_is_consistent`
- [x] A push to `main` runs `ubsan.yml`, it is green, and `sanitizer_smoke_*` ran and passed. `manual: push; read the run log`
- [ ] A deliberate signed overflow in a tested path turns the run red, and the artifact holds the report. `manual: throwaway branch, as in ticket 03`
- [x] The ubsan badge renders. `manual: view README on github.com`

## Comments

- ubsan.yml added with badge; docs/ci-checks.md 'Every change' lists it. Whether the runner's gcc accepts -fsanitize=float-cast-overflow is checked by the first run; nothing was dropped.
