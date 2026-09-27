# 09: `-Wswitch-enum`

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 08

## What

`-Wswitch-enum` warns when a `switch` on an enum has a `default` and does not
list every enumerator. That is the state-machine bug class: add a state, forget
the switch, and the `default` silently takes it. It matters most in
`lifecycle_model.c` and `lifecycle.c`, the lifecycle table, the core of the
scheduler. 88 warnings (2026-09-26, `bb9d69f`): `lifecycle_model.c` 67,
`lifecycle.c` 12, `transcode.c` 4, `luacbor.c` 3, `workflow.c` 2.

It is not a warning to turn off because "the default handles it": the default
handles it *wrongly* the day the enum grows. The right code is one of

- a switch on the enum that lists every enumerator and has no `default`
  (then `-Wswitch` in `-Wall` reports the missing one), or
- a `default` that is a small helper that aborts with the file and line (the
  way `qwe_xfmt` and the lifecycle shell already abort on an impossible event), and
  every enumerator listed above it.

`lifecycle_model.c` (67) may be a table with a `switch` per state: read it first;
if the switch is really a lookup, replace the switch with a table indexed by the
enum and a static assertion on the table's length
(`_Static_assert` is C11: the file is C99, so use the
`typedef char qwe_assert_n[(N) == QWE_LC_NSTATES ? 1 : -1]` form or a runtime
`assert` in the table's init test, which `lifecycle_test`'s existing layers
already do for the model).

## Acceptance criteria

- [ ] Zero `-Wswitch-enum` in `src/` and `tools/`, and `-Wswitch-enum` in
      `.bazelrc` with `-Werror`. `manual: bazel build //...`
- [ ] No switch on a `qwe_lc_*` enum has a bare `default` that returns a value.
      `manual: grep -n "default:" src/kernel/lifecycle*.c`, each named in the
      Comments
- [ ] Adding an enumerator to `enum qwe_lc_state` (throwaway) fails the build or
      the `lifecycle_test` model layer, in each place that must handle it.
      `manual: add one, see the failures, revert; list them`
- [ ] The lifecycle tests are unchanged in behaviour: same scenarios, same
      count. `unit: src/kernel/lifecycle_test.c`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
