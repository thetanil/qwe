# 05: Reserved identifiers: an allow list, not an exclusion

Status: resolved
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

- [x] `.clang-tidy` enables `bugprone-reserved-identifier` and `cert-dcl37-c` with `AllowedIdentifiers` set to the five anchored patterns, and the gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] A new reserved name fails the gate. `manual: add a throwaway static int __qwe_tmp; to a src/kernel/*.c file, run the gate, see it exit non-zero naming bugprone-reserved-identifier; revert`
- [x] The doc row says "narrowed to …" and lists the allowed names with the reason for each. `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

`bugprone-reserved-identifier` and `cert-dcl37-c` are enabled, each with
`AllowedIdentifiers` set to `^_POSIX_C_SOURCE$;^_GNU_SOURCE$;^__wrap_.*$;^__real_.*$;^__executable_start$`
(the check does not anchor its regexes, so they are anchored here). `cert-dcl51-cpp`
stays excluded; its doc row now says it is a C++ rule that cannot fire on C.

Both options are needed: the alias reads its own option name, so setting only
`bugprone-reserved-identifier.AllowedIdentifiers` would leave `cert-dcl37-c`
reporting the names. Gate exit 0.

Negative check: `int __qwe_tmp(void) { return 0; }` appended to
`src/kernel/clock_test.c` failed the gate with `declaration uses identifier
'__qwe_tmp', which is a reserved identifier [bugprone-reserved-identifier,cert-dcl37-c]`.
Reverted. (A first try with `static int __qwe_tmp;` never reached the analyzer,
since gcc's `-Werror=unused-variable` failed the Bazel build first.)

`run.sh --raw` now reports `bugprone-reserved-identifier` as `option`: 75 (the
ticket's 74 plus the `_POSIX_C_SOURCE` define in `put_test.c` from ticket 04),
and "hidden by a CheckOptions narrowing: 75".

`bazel test //...` 258 pass, 3 skipped; coverage check green.
