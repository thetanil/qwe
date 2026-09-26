# 01: `--raw` sees what `CheckOptions` narrows

Status: ready-for-agent
Category: bug
Type: task

## What

`tools/clang-tidy/run.sh --raw` is supposed to show the whole backlog: every
group `.clang-tidy` enables, with none of its exclusions. It resets the
exclusions with `--checks='-*,<groups>'`, but it still reads `.clang-tidy`, so
the `CheckOptions` apply. `sca-findings/04` narrowed `cert-err33-c` with
`CheckedFunctions`, so `--raw` now reports 0 `cert-err33-c` findings. The real
count is 173, all `fprintf`/`fputs`/`fputc`. This feature turns more exclusions
into options (05, 06, maybe 09), so the blind spot would grow with each one.

Measured at `2d56de5`:

| | total | `cert-err33-c` |
|---|---|---|
| `run.sh --raw` today | 254 | 0 |
| raw ruleset with `CheckOptions` removed | 427 | 173 |

The second row came from a config copy with the `CheckOptions:` block cut out,
passed with `--config-file=` (clang-tidy 20 does not merge `--config-file`
with `--config`, so the options cannot be blanked on the command line).
`--raw` should do the same with a temp file. It should also report the
narrowed findings as their own number, so a reader can tell "excluded" from
"narrowed". For example, add a `narrowed` column, or a second tally line
that counts what the options hide.

Keep the rest of `--raw` as it is: it counts each `file:line:col:check` once,
splits `src/`+`tools/` from `*_test.c`, and always exits 0. Don't assume
`CheckOptions` is the last block in `.clang-tidy`: parse it, or put a
marker comment around it.

## Acceptance criteria

- [ ] `run.sh --raw` at this ticket's base reports 427 findings (or the count after intervening fixes, with the difference explained), including 173 `cert-err33-c`. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh --raw`
- [ ] The output tells findings hidden by a `CheckOptions` narrowing apart from findings hidden by an exclusion. `manual: the same run`
- [ ] The default gate is unchanged and exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] `docs/static-analysis.md`'s `--raw` section drops the "One blind spot" paragraph and says that raw mode ignores `CheckOptions`. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` is green. `unit: bazel test //...`

## Comments
