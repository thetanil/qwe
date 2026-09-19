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
