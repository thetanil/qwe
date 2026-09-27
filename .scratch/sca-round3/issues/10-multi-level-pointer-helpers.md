# 10: Typed pointer-array helpers, and `multi-level-implicit-pointer-conversion` on

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 02

## What

`bugprone-multi-level-implicit-pointer-conversion` is excluded, with the reason
"the only way to quiet it is a cast at each of the 50". That is not true, and the
exclusion hides the check's real target: `free(&p)`, `qsort(&arr, ...)`,
`memset(&p, ...)` with the address of the pointer where the pointer was meant.
(Those compile, and `free(&p)` on a local is only caught when the analyzer's
`unix.Malloc` happens to see it.)

At `407f76a` the 50 findings are: `free` 27, `qsort` 3, the argument of
`realloc` 5, the result of `realloc` 5, `calloc` 5, `qwe_xcalloc` 2, `malloc` 1,
and 2 for `luacbor.c`'s `key_cmp` (`const void *` to `const char *const *`).
By file: `luaexec.c` 15, `luacbor.c` 9, `workflow.c` 7, `run.c` 7, `validate.c` 5,
`luafs.c` 5, `jobs.c` 2. They are all one shape: an array of `char *`
(argv, names, keys, ids, `needs`), allocated, grown, sorted and freed.

## Fix

One small header, `src/kernel/strv.h` (`//src/kernel:strv`), that owns that shape
and is the only place a `T **` meets a `void *`, with the conversion **explicit**
(the check does not report an explicit cast, and an explicit cast in one
reviewed function is what the check's own message asks for):

```c
struct qwe_strv { const char **v; size_t n, cap; };   /* or char ** where owned */
int  qwe_strv_push(struct qwe_strv *, const char *);  /* grows; -1 on OOM */
void qwe_strv_free(struct qwe_strv *);                /* frees v, not the strings */
void qwe_strv_sort(struct qwe_strv *);                /* one strcmp comparator */
```

plus an `_owned` flavour that also frees the strings. Convert the 50 sites to it
(the `argv`, `names`/`values`, `keys`, `ids`, `group_names` arrays). Where the
array is fixed-size and built once (`calloc(n + 1, ...)` for `argv`), a
`qwe_strv_new(n)` takes the same path.

`luacbor.c`'s `key_cmp` (`const void *` to `const char *const *`) is the only
`qsort` comparator over a `char *` array; its keys become a `qwe_strv` and
`qwe_strv_sort` replaces the `qsort` call, so `key_cmp` is deleted. (The other
comparators, `positions.c` `cmp` and `validate.c` `cmp_problem`, sort arrays of
structs: one pointer level, not this check's business; ticket 11 handles them.)
The one comparator in `strv.c` uses `a` and `b` in one expression so ticket 11's
check sees them used together.

This interacts with ticket 02 (`exec_run`/`exec_preamble` are the biggest users):
do 02 first, then convert.

## Acceptance criteria

- [ ] `.clang-tidy` no longer excludes the check and has no option for it. The
      gate exits 0. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
- [ ] The only `T **` to/from `void *` in `src/` and `tools/` is in `strv.c`,
      each explicit. With the check on and the gate green there is no implicit
      one anywhere; for the explicit ones, every cast to a two-level pointer type
      is in `strv.c`. `manual: grep -rnE "\((const )?[a-z_ ]+\*[[:space:]]*(const[[:space:]]*)?\*\)" src tools --include=*.c --include=*.h | grep -v strv.c`
      prints nothing, or only casts the Comments show are not to/from `void *`
- [ ] `key_cmp` is gone from `luacbor.c`. `manual: grep`
- [ ] `free(&p)` on a local fails the gate. `manual: add a throwaway in
      src/kernel/clock_test.c, see the check named, revert`
- [ ] `strv` has a unit test for push growth, OOM (through the `oom_shim`), sort
      order and free. `unit: src/kernel/strv_test.c`
- [ ] The doc row for this check is gone. `manual: docs/static-analysis.md`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
