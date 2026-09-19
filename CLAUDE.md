qwe - Quantum Workflow Engine

qwe is a lightweight linux command line workflow engine in pure C99 with a
github action like yaml workflow specification which uses luajit plugins

the plugin kernel: thetanil/qwe/docs/workflow-kernel-design.md

the old design with python: thetanil/qwe/docs/device-manager-spec.md

the concept of the application we were building in thetanil/qwe/docs/device-manager-spec.md
is still valid as the first plugins to implement in qwe, hower this project uses NO PYTHON

There is never python used in this repo

we use bazel 8.7.0 which is already installed in the environment

all sources for the single binary are vendored into this repo and built with bazel

## Agent skills

### Issue tracker

Issues and specs live as local markdown under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `CONTEXT.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Working rules

- **Issues are raw markdown, not the `issues` CLI.** This project overrides the workspace rule: tickets live in `.scratch/<feature>/issues/NN-<slug>.md` and are edited by hand. Closing a ticket means `Status: resolved`, ticked `- [x]` acceptance criteria, and notes appended under `## Comments`.
- **One commit per closed ticket.** This project overrides the workspace "never commit" rule, for this project only. Once `bazel test //...` is green and the ticket file is updated, commit (ticket update included). Never commit before both are true. **Never push.**
- **Do not overwrite existing files wholesale** (for example `.gitignore`). Read and edit them. `.gitignore` already ignores `/bazel-*` and `.qwe/runs/`.
- **e2e tests are named `<case>_test`.** The `e2e_test` macro in `tests/e2e/defs.bzl` adds the suffix, because a test named after its case directory shadows that directory in runfiles. Case layout is documented at the top of `tests/e2e/run_case.sh`.
- **Stub subcommands** print `qwe <cmd>: not implemented` to stderr and exit 2.
