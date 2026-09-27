# 16: `cert-dcl51-cpp`

Status: ready-for-agent
Category: enhancement
Type: task

## What

`.clang-tidy` still excludes `-cert-dcl51-cpp`, with the doc row "a C++ rule; it
never fires on C, so excluding it hides nothing". If it cannot fire, enabling it
costs nothing, and removes an exception line an assessor has to read and accept.
Evidence it does not fire on C: it is an alias of `bugprone-reserved-identifier`
registered for C++ only; every reserved-identifier diagnostic printed this
session names `[bugprone-reserved-identifier,cert-dcl37-c]` and never
`cert-dcl51-cpp`.

## Fix

Delete the line. Run the gate. If it reports anything, that contradicts the
premise: read the finding and fix the code.

## Acceptance criteria

- [ ] `.clang-tidy` has no `-cert-dcl51-cpp`. The gate exits 0. `manual: run.sh`
- [ ] The doc row is gone. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green.

## Comments
