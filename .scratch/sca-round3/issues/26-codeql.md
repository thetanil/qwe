# 26: CodeQL on a real build

Status: ready-for-agent
Category: bug
Type: task

## What

`.github/workflows/codeql.yml` is the generated template with `actions` and
`c-cpp`, both at `build-mode: none`, `config-file: .github/codeql/codeql-config.yml`
(`paths-ignore: third_party`), and the `queries:` line commented out, so it runs
the **default** suite. Three consequences:

- `build-mode: none` for C/C++ extracts without a compiler run. It sees no
  generated header (`src/kernel/lua/embedded.h`, LuaJIT's buildvm output), no
  macro expansion under the real flags, and no `-D` from the build. Bazel is not
  understood by the autobuild, so "none" was the easy choice, and the weakest.
- The default suite is the smallest. `security-and-quality` adds the correctness
  and maintainability queries, `security-extended` adds more security ones.
- The README says "Not a gate", and `docs/static-analysis.md` does not mention
  CodeQL at all. An analysis that is not a gate and not in the safety case is not
  evidence.

## Fix

- Switch `c-cpp` to `build-mode: manual` with `bazel build //src/... //tools/...`
  (the same runner image and GCC as the other CI builds; ticket 04 pins
  clang-tidy only; `--spawn_strategy=local` so CodeQL's
  tracer sees the compiler), and `actions` stays at `none`.
- `queries: security-and-quality` (`+security-extended` if the two do not
  overlap: check what each contains), in the config file.
- Run it on `pull_request` and `push` as now and on the schedule, and make it a
  required check in branch protection if the repo's flow uses them (ticket 04).
- The first run's alerts are tickets, and are fixed in code. An alert dismissed
  "won't fix" or "false positive" in the UI is an exception with no record in
  the repo: forbid it here, and if one is unavoidable, add a CodeQL query filter
  in `codeql-config.yml` with a comment naming the ticket.
- Document it in `docs/static-analysis.md`: what it runs, what it is compared to
  (overlap with clang-tidy, what only CodeQL finds), where to read the results.

CodeQL cannot run here (no CLI in the devcontainer), and the first real run
needs a push, which this project's agents never do (CLAUDE.md). So the agent
does everything up to the push, then sets `Status: ready-for-human` with the
steps. The human pushes and pastes the alert list; fixing the alerts is then
agent work again, in this ticket or in new ones.

## Acceptance criteria

- [ ] `codeql.yml` builds C with Bazel under CodeQL's tracer and runs
      `security-and-quality`. `manual: read the workflow; a run is green`
- [ ] The alert list of the first run is in the Comments, with a fix or a ticket
      per alert; open alerts at the end: zero. `human: the Security tab, linked, after the push`
- [ ] `docs/static-analysis.md` covers CodeQL. `manual: doc`
- [ ] `tools/ci/workflows_test.sh` still passes. `unit: workflows_test.sh`
- [ ] `bazel test //...` is green.

## Comments
