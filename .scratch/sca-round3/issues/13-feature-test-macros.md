# 13: Feature-test macros into the build flags

Status: ready-for-agent
Category: enhancement
Type: task

## What

`bugprone-reserved-identifier` and `cert-dcl37-c` run with an allow list
(`AllowedIdentifiers`) of five names. Two of them, `_POSIX_C_SOURCE` and
`_GNU_SOURCE`, account for 33 of the 75 sites (`_POSIX_C_SOURCE` 22+,
`_GNU_SOURCE` 11+ at `407f76a`), each a `#define` at the top of a `.c` file. They
are the standard's own mechanism for selecting a feature set, and defining one is
not "using a reserved identifier" as a bug, but every allowed name is an item in
an exception list an assessor reads. The name does not have to appear in source at
all: a feature-test macro can come from the compiler command line.

## Fix

- Add the macro to the build, for `src/` and `tools/` only and in **every**
  configuration a file is built in. Two traps, both measured in `.bazelrc`:
  - A plain `--copt` also reaches `third_party/` (LuaJIT, libsodium and others
    set their own feature macros; a predefined `_POSIX_C_SOURCE` can change
    which declarations their headers see, and `-w` hides the redefinition).
  - `--copt` does not reach the exec/host configuration, which gets only
    `--host_copt=-std=c99`. `tools/bcembed.c` and `src/kernel/errstr.c` are
    built there (ticket 05 counts three configurations each), so they would
    silently lose the macro.

  So use `--per_file_copt='^(src|tools)/.*@-D_POSIX_C_SOURCE=200809L'` together
  with the matching `--host_per_file_copt`, or a shared copts constant in a
  `.bzl` that every `cc_*` target under `src/` and `tools/` uses (then
  `copts`/`local_defines` follow the target into every configuration). Say in
  the Comments which one was chosen and why. For the GNU set, per target
  `local_defines = ["_GNU_SOURCE"]` on the targets that need it
  (`open_memstream` is POSIX 2008; `fopencookie`, `pipe2`, `asprintf`,
  `strerrorname_np` and `getresuid` need `_GNU_SOURCE`); a target mixing files
  that need it with `errstr.c` is split.
- **`errstr.c` must stay on the XSI `strerror_r`**: it defines
  `_POSIX_C_SOURCE` *without* `_GNU_SOURCE` on purpose (ticket 06 of round 2). Keep
  it out of any `_GNU_SOURCE` `local_defines`, and say so in a BUILD comment.
- Delete every `#define _POSIX_C_SOURCE` and `#define _GNU_SOURCE` from `.c` and
  `.h` files. `tools/clang-tidy/run.sh` already passes `-D` flags from the
  Bazel action to clang-tidy, so the analyzer sees the same macros.
- A test that fails if a source file defines or undefines a feature-test macro,
  allowing for whitespace after the `#`:
  `grep -rnE "^[[:space:]]*#[[:space:]]*(define|undef)[[:space:]]+_(POSIX_C_SOURCE|GNU_SOURCE|XOPEN_SOURCE|DEFAULT_SOURCE|BSD_SOURCE|SVID_SOURCE)\b" src tools`.

## Acceptance criteria

- [ ] No `#define _POSIX_C_SOURCE`/`_GNU_SOURCE` in `src/` or `tools/`.
      `unit: tools/ci/feature_test_macros_test.sh`
- [ ] `_POSIX_C_SOURCE` and `_GNU_SOURCE` are gone from `AllowedIdentifiers` in
      `.clang-tidy`, and the gate exits 0. `manual: run.sh`
- [ ] Every target still builds under `-std=c99` (and under `--config=asan`,
      `ubsan`, `valgrind`, `fuzz`, `coverage`). `manual: bazel build` for each
- [ ] The exec-configuration builds of `bcembed.c` and `errstr.c` get the macro.
      `manual: bazel aquery 'mnemonic("CppCompile", //tools:bcembed)'` shows
      `-D_POSIX_C_SOURCE=200809L` on every action, exec included
- [ ] No `third_party/` compile action gains the macro. `manual: bazel aquery`
      on a LuaJIT and a libsodium file shows no `-D_POSIX_C_SOURCE`
- [ ] `errstr_test` still passes with the XSI `strerror_r`. `unit: errstr_test`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
