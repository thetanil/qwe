# 11: Swappable parameters in `src/` and `tools/`

Status: ready-for-agent
Category: enhancement
Type: task
Blocked by: 10, 12

## What

`bugprone-easily-swappable-parameters` is excluded. Round 2 closed it `wontfix`
with "a class of mistake this codebase has not made". The check was off the
whole time, so it could not have found one; that is not evidence. 21 findings in
`src/` and `tools/` at `bb9d69f` (30 in all, 9 in tests: ticket 12), all with the
default options (`MinimumLength: 2`, `ModelImplicitConversions: true`,
`SuppressParametersUsedTogether: true`). No option is to be turned: an option is
an exception.

| Function | File | Pair | Fix |
|---|---|---|---|
| `qwe_positions_add` | `positions.c:40` | `key`, `value` (`const struct qwe_pos *`) | reorder is not enough: pass a `struct qwe_pos_pair` |
| `cmp` | `positions.c:78` | `a`, `b` (`const void *`) | comparator: use `a` and `b` in one expression |
| `check_props` | `transcode.c:265` | `anchor`, `tag` (`const yaml_char_t *`) | a `struct yaml_props { anchor, tag }` |
| `qwe_xstrdup_at` | `alloc.c:42` | `s`, `file` (`const char *`) | order is fixed by the macro; a `struct qwe_site { file, line }` |
| `qwe_lc_event_is_stale` | `lifecycle.c:257` | enum state/event and two `long` steps | a struct for the two steps |
| `key_cmp` | `luacbor.c:220` | `a`, `b` | deleted by ticket 10 (`qwe_strv_sort`) |
| `convert_container` | `luacbor.c:73` | `depth`, `is_map` (`int`) | `enum { MAP, ARRAY }` for `is_map` |
| `qwe_oom_probe` | `oom_shim.c:146` | `n`, `child_n` (`long`) | a `struct qwe_oom_arm { self, child }` |
| `qwe_preamble_build` | `preamble.c:29` | `names`, `values`; two `size_t *` out-params | a `struct qwe_env { names, values, n }` in, a result struct out |
| `qwe_sched_pass` | `sched.c:18` | `size_t n`, `long max_parallel` | struct |
| `group_used` | `sched.c:4` | `n`, `group` (`size_t`) | struct or reorder around distinct types |
| `qwe_summary_render`, `qwe_summary_write` | `summary.c:152,233` | `run_dir`, `workflow_file`, `overall_outcome` | a `struct qwe_summary_ctx` |
| `qwe_trace_record` | `trace.c:32` | `long step`, enum, `long duration` | struct |
| `cmp_problem` | `validate.c:109` | `a`, `b` | one expression |
| `job_event` | `workflow.c:1389` | `enum evkind kind`, `long step` | struct or a step-typed field |
| `load_inventory` | `workflow.c:435` | `cmd`, `wf_path`, `inv?` (`const char *`) | struct |
| `ev_add` | `workflow.c:564` | `enum evkind`, `int fd`, `long step` | struct |
| `send_result` | `workflow.c:64` | `idx`, `fd` (`int`) | reorder to `fd` first is not enough: a `struct qwe_fd { int fd; }` or pass a `struct step_result_sink *` |
| `ssh_call` | `workflow.c:850` | `fn`, `a`, `b` (`const char *`) | struct of arguments |

(The table lists what the check printed for each. Open the function and read what
the pair means before choosing a fix; the "Fix" column is a suggestion.)

## Fix

Change the signature so a swapped call **fails to compile**, in order of
preference:

1. A struct with named fields, passed by pointer or value
   (`qwe_summary_write(path, &(struct qwe_summary_args){.run_dir = ..., ...})`).
   Callers name the fields, so the swap becomes a visible change.
2. A distinct type for each argument (`struct qwe_fd { int v; }`), where a
   value is one integer with a meaning.
3. Reordering so no two adjacent parameters have a convertible type, only where
   the call sites are few and the ordering is natural.

Comparators for `qsort`: their signature is fixed (`const void *, const void *`),
so make the check see `a` and `b` "used together" (the check's own
`SuppressParametersUsedTogether`, not an exception). The `char *` comparator
(`luacbor.c` `key_cmp`) is gone after ticket 10. The two left sort arrays of
structs and compare field by field over several statements (`positions.c`
`cmp`, `validate.c` `cmp_problem`), so "one expression" does not fit their
bodies directly. Give each a typed function and make the comparator a one-line
trampoline that passes both parameters to it in one call:

```c
static int problem_order(const struct problem *x, const struct problem *y);
static int cmp_problem(const void *a, const void *b)
{
	return problem_order(a, b);
}
```

Both parameters go to one call, which the check counts as used together; the
typed function has two parameters of one struct pointer type, so check that the
check does not report *it* in turn (if it does, it takes a pair struct or the
order is made explicit by name, e.g. `problem_before(earlier, later)`, and the
Comments say which). No `NOLINT`.

Each signature change is small and mechanical; do them in dependency order (leaf
functions first) and run the tests after each. Do not leave a struct-wrapping
shim beside the old signature.

## Acceptance criteria

- [ ] `.clang-tidy` does not exclude the check and has no option for it; the gate
      exits 0 for `src/` and `tools/`. `manual: CLANG_TIDY=clang-tidy-20 bash tools/clang-tidy/run.sh`
      (tests: ticket 12; do 12 first or in the same change so the gate is green)
- [ ] For each function above, a swapped call site no longer compiles or no
      longer type-checks. `manual: the Comments list the replaced signatures`
- [ ] No `NOLINT` and no option was added for the check. `manual: git diff .clang-tidy`
- [ ] `bazel test //...` is green, with the same e2e goldens. The coverage check
      is green.

## Comments
