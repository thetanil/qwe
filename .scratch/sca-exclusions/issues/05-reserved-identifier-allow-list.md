# 05: Reserved identifiers: an allow list, not an exclusion

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 01

## What

`bugprone-reserved-identifier` and its aliases `cert-dcl37-c` and
`cert-dcl51-cpp` are excluded because the code has to use reserved names. The
74 raw findings are exactly those names:

| Identifier | Findings |
|---|---|
| `_POSIX_C_SOURCE` | 22 |
| `__wrap_*` | 20 |
| `__real_*` | 20 |
| `_GNU_SOURCE` | 11 |
| `__executable_start` (`oom_shim.c:76`, a linker symbol) | 1 |

The reason in the doc is right, but an exclusion is too broad for it: it lets
through every other reserved name, such as a `_Foo` type or a `__helper`.
clang-tidy 20 has `AllowedIdentifiers`, a `;`-separated list of regexes, on
`bugprone-reserved-identifier` and on `cert-dcl37-c`. Re-enable both with the
list set to those five patterns, anchored. `cert-dcl51-cpp` is a C++ rule
that never fires on C, so keep it excluded and say that in the doc row.

Ticket 01 must land first, so `--raw` still counts the 74 once they are
allowed by an option.

## Acceptance criteria

- [ ] `.clang-tidy` enables `bugprone-reserved-identifier` and `cert-dcl37-c` with `AllowedIdentifiers` set to the five anchored patterns, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] A new reserved name fails the gate. `manual: add a throwaway static int __qwe_tmp; to a src/kernel/*.c file, run the gate, see it exit non-zero naming bugprone-reserved-identifier; revert`
- [ ] The doc row says "narrowed to …" and lists the allowed names with the reason for each. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
