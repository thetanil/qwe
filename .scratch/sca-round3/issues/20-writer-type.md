# 20: A writer type that carries its own error

Status: ready-for-agent
Category: enhancement
Type: task

## What

`src/kernel/put.h` gives writers `qwe_out_str`, `qwe_out_ch` and `qwe_out_fmt`,
which take a `FILE *`, return nothing, and carry one `(void)` cast. The
justification is "a stdio stream's error flag is sticky, so the writer checks
`ferror(fp)` once, before `fclose`". Nothing enforces the second half. A writer
that uses `qwe_out_str` and forgets `ferror` passes the compiler, both
analyzers, every test, and the gate; cert-err33-c used to flag every unchecked write; the helper made that
silent, and put nothing in its place. It is a rule kept by convention. An assessor asks "how do you know every writer checks?" and the
answer today is "we read them".

Users: `result.c`, `summary.c`, `bcembed.c`, `workflow.c`'s `report_disabled`,
and the fixture writers in five test files.

## Fix

Make the error part of the type, so forgetting is a type error or a resource
leak the analyzers see:

```c
struct qwe_out { FILE *fp; int failed; };
int  qwe_out_open(struct qwe_out *o, const char *path, const char *mode);
void qwe_out_wrap(struct qwe_out *o, FILE *fp);     /* for a cookie or memstream */
void qwe_out_str(struct qwe_out *o, const char *s);  /* sets o->failed, never fails silently */
...
int  qwe_out_close(struct qwe_out *o);              /* ferror + fclose; -1 if either */
```

`qwe_out_str` etc. check the return themselves and set `failed`: no cast, no
"ignored" return at all. `qwe_out_close` is the one exit and returns the
combined result; a writer that never calls it leaves an open `FILE *` that the
stream checker (`clang-analyzer-unix.Stream`) reports, which is the check we
already have on. **That only works if the analyzer can see the `fopen`.** It
analyses one translation unit at a time (ticket 06), so a `qwe_out_open` defined
in `put.c` is a black box to `summary.c` and the leak is invisible. Define
`qwe_out_open` and `qwe_out_close` as `static inline` in `put.h` (they are a few
lines each), or rely on CTU only if ticket 06 adopted it; say which in the
Comments. Even then the checker only tracks a stream opened in the function it
is analysing (plus what it inlines), so a writer handed an already-open
`struct qwe_out *` is covered by the caller's check, not its own: state that
limit in `docs/static-analysis.md`'s `qwe_out` idiom entry rather than claiming
more. `qwe_summary_render` takes a `struct qwe_out *`, so its
`mid_stream_write_failure_fails` test still works through `qwe_out_wrap` on a
cookie stream. The existing behaviour is unchanged.

`qwe_diag` (stderr) is a separate idiom and not part of this ticket (spec: not
ticketed).

## Acceptance criteria

- [ ] `put.h` no longer has `(void)` casts of `fputs`/`fputc`/`vfprintf` on a
      data stream; `qwe_diag`'s remains. `manual: grep -n "(void)" src/kernel/put.h`
- [ ] A writer that opens a `struct qwe_out` and never closes it is reported by
      the gate. `manual: a throwaway in summary.c's opening function that skips
      qwe_out_close; see the unix.Stream finding; revert` (if it is not
      reported, the open/close are not visible to the analyzer: see Fix)
- [ ] A writer that ignores `qwe_out_close`'s result is reported by the gate
      (declare it `__attribute__((warn_unused_result))`; `-Werror` makes it an
      error). `manual: throwaway`
- [ ] The same tests as before pass, plus `put_test` for `failed` being set by a
      failing write and returned by `close`. `unit: src/kernel/put_test.c`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
