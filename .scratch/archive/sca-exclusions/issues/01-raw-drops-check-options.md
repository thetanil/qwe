# 01: `--raw` sees what `CheckOptions` narrows

Status: resolved
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

- [x] `run.sh --raw` at this ticket's base reports 427 findings (or the count after intervening fixes, with the difference explained), including 173 `cert-err33-c`. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh --raw`
- [x] The output tells findings hidden by a `CheckOptions` narrowing apart from findings hidden by an exclusion. `manual: the same run`
- [x] The default gate is unchanged and exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [x] `docs/static-analysis.md`'s `--raw` section drops the "One blind spot" paragraph and says that raw mode ignores `CheckOptions`. `manual: docs/static-analysis.md`
- [x] `bazel test //...` is green. `unit: bazel test //...`

## Comments

- `--raw` writes a copy of `.clang-tidy` without its `CheckOptions:` block (awk: skip from `CheckOptions:` to the next top-level key, so the block need not be last) to a temp file and passes it with `--config-file`. Checked: `--config` cannot do it. With `--config`, `--dump-config` shows `HeaderFilterRegex: ''`, so it replaces the file rather than merging, and `--config-file` plus `--config` is an error.
- Excluded vs narrowed is one pass, not two. Each check is classified by `.clang-tidy`'s own `Checks` list: a check matched by a `-` line is `excluded`, and any other check is `option`. An enabled check can only be hidden from a green gate by an option. The doc states that this assumes the gate is at exit 0. The exclusions are matched as anchored globs, so a future `-group-*` line works.
- Output at `dcc74d8`: total 427 (281 src+tools, 146 `*_test.c`); `hidden by an exclusion: 254; by a CheckOptions narrowing: 173`. `cert-err33-c` 144 + 29, marked `option`. It matches the hand-built scan the spec was measured with.
- Gate exits 0 (default mode untouched). `bazel test //...` green (257 pass, 3 skipped). shellcheck clean.
