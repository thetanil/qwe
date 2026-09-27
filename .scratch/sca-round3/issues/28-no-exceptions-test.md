# 28: "No exceptions" enforced by a test

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 05, 06, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24

## What

The point of this round is that the number of exceptions is small and known.
That is only true as long as nobody adds one. Today an exclusion is one line in
`.clang-tidy` and a `NOLINT` is one comment; both pass the gate. Make adding one
fail a test, so it has to be argued for in a ticket and in this test's allow-list
(reviewed, versioned, and short) and not slip in.

An option is an exception only when it makes a check see **less**. Ticket 06
may turn analyzer settings on (`ModelPOSIX=true`, CTU) and ticket 24 adds
`max_cyclomatic_complexity`/`max_line_length` to `.luacheckrc`: those make the
tools stricter and are configuration, not exceptions. The test has to tell the
two apart rather than ban every option.

## Fix

`tools/ci/no_exceptions_test.sh` (a Bazel `sh_test`) checks, from the repo:

1. **`.clang-tidy` `Checks`**: the first entry is `-*` (the baseline: everything
   off, then the groups switched on); after it there is no `-check` entry apart
   from the allow-list.
2. **`CheckOptions`**: every key is listed in `tools/ci/exceptions.txt` with a
   kind:
   - `widen`: the option makes the check report more than its default (an
     analyzer mode switched on, a threshold lowered). Recorded with its ticket;
     not an exception, not in the assessor's exception register, but it cannot
     be added silently either, because a "widening" that is really a narrowing
     is exactly the thing to catch in review.
   - `narrow`: anything else. This is an exception, with its ticket and the
     sentence that justifies it. Ideally none.

   A key not in the file fails the test.
3. **No `NOLINT`, `NOLINTNEXTLINE`, `NOLINTBEGIN`** in `src/`, `tools/`,
   `plugins/`, apart from the allow-list (expected: the one on `getenv` in
   `src/kernel/env.c`, ticket 17). An allow-list entry names the file and the
   check, not a line number, so it survives edits but not a second `NOLINT` in
   the same file.
4. **No `#pragma GCC diagnostic`**, no `-Wno-*` in `.bazelrc`/BUILD files for
   our code (third_party's `-w` and `per_file_copt` are recorded in ticket 27), no
   `__attribute__((unused))` on a variable to silence a warning, apart from the
   allow-list.
5. **Nested `.clang-tidy` files**: exactly `src/testing/wrap/.clang-tidy` and
   `src/kernel/gcov/.clang-tidy` (ticket 15), and the content of each is exactly
   `InheritParentConfig: true` plus its own `AllowedIdentifiers` (compared to a
   fixed text in the test).
6. **Lua**: no `-- luacheck:` comment in any `.lua` outside `third_party/`, and
   no `ignore`/`globals`/`read_globals`/`allow_defined*` in `.luacheckrc`
   (tickets 23, 24). Its numeric limits (`max_cyclomatic_complexity`,
   `max_line_length`) are listed in `exceptions.txt` as `widen`, or as `narrow`
   if ticket 24 had to set one above its starting value.
7. **`run.sh --raw` in a "count" mode** (its output is a table today): the raw
   tally of every enabled group without exclusions or `narrow` options equals
   the gate's tally, i.e. the gate hides nothing. `--raw` must also apply the
   nested `.clang-tidy` files (ticket 15) and the `widen` options.

`exceptions.txt` is the exception register (its `narrow` rows are the
assessor's reading list); ticket 29's doc is checked against it.

## Acceptance criteria

- [ ] The test exists and passes. `exceptions.txt` has no `narrow` row beyond
      the ticketed ones (expected: the `env.c` `getenv` `NOLINT`, the two nested
      reserved-identifier configs). `unit: tools/ci/no_exceptions_test.sh`
- [ ] Each rule fails on a throwaway violation: an exclusion, an unlisted
      `CheckOptions` key, a `NOLINT`, a `#pragma GCC diagnostic`, a third nested
      `.clang-tidy`, an extra pattern in a nested one, a `-- luacheck: ignore`.
      `unit: the test has one negative case per rule, built on a copy of the tree,
      the way tools/ci/workflows_test.sh does`
- [ ] `run.sh --raw`'s "hidden by an exclusion / option" both read 0.
      `manual: run.sh --raw`
- [ ] `bazel test //...` is green.

## Comments
