# 08: Audit the bare allocation calls

Status: resolved
Category: enhancement
Type: task
Blocked by: 03, 04, 05, 06, 07

## What

Ticket 02 closed with its first criterion, "every allocation outside the test
files is either checked at its call site or made through an aborting helper",
ticked from the implementation notes and not re-verified. When it closed, a
grep for the bare calls still returned 63 sites outside `alloc.c` and the tests:

```
grep -rnE '\b(malloc|calloc|realloc|strdup)\(' src plugins tools --include=*.c \
  | grep -v '_test.c\|src/kernel/alloc.c' | grep -vE 'qwe_x(malloc|calloc|realloc|strdup)'
```

The criterion allows checked call sites, so a nonzero count is not itself a
failure. This ticket reads each one and confirms it.

It waits for the rest of `quality/` on purpose. By then the tools that find
what reading misses exist: the sanitizer configs (03), the valgrind gate (04),
the coverage report (05), the fuzzers (06) and the OOM-injection harness (07).
The audit uses them instead of doing everything by eye: a site that coverage
says is never reached, or that injection never fails, is the one to read
hardest.

Sites at the time of writing, by file: `validate.c` 13, `workflow.c` 12,
`jobs.c` 5, `luaexec.c` 5, `transcode.c` 4, `envelope.c` 4, `positions.c` 3,
`luacbor.c` 3, `redact.c` 3, `dag.c` 2, `luafs.c` 2, `bcembed.c` 2, and one each
in `encrypt.c`, `run.c`, `preamble.c`, `ring.c`, `sink.c`. The count will have
moved; rerun the grep.

For each site decide one of:

1. **Checked here**, and the failure is reported (load, validate, job setup) or
   handed to the parent (forked child), as rule 1 and rule 3 of
   `src/kernel/alloc.h` say. Nothing to do beyond confirming the branch runs
   under injection (07).
2. **Should use the helper.** Nothing can report the failure, so it should
   abort naming the site (rule 2). Convert it.
3. **Unchecked.** A bug: check it, or convert it, and add the injection case.
4. **Not a runtime allocation** (`tools/bcembed.c` is a build-time tool).
   Record it as exempt with the reason.

Pay particular attention to two kinds of site:
an allocation made while reporting some other error (`validate.c`), where
failing to report is worse than aborting, and `realloc` results assigned back
over the only pointer to the block.

## Acceptance criteria

- [x] Every remaining bare call is listed in this ticket's comments with its verdict (1 to 4 above), and no site is left unclassified.
- [x] Every site classified 2 or 3 is converted or checked, and each has an OOM-injection case from ticket 07. `unit: the injection cases named per site in the comments`
- [x] Every site classified 1 is confirmed reached by the injection harness or by an existing failure test; a site that no test can reach is noted with why.
- [x] A check keeps the count from creeping back: either the grep above is a test that compares against an allowlist of exempt files, or the exemption list is written next to the helper. `unit or script: fails when a new bare call appears outside the allowlist`
- [x] `bazel test //...`, `--config=ubsan` and `--config=asan` are green.

## Comments

### Resolved

Method: read all 64 sites (63 in the ticket plus `strndup`, which its grep missed:
`validate.c` 1, `workflow.c` 1, `bcembed.c` 1; the grep in `alloc_audit_test` includes it), then
put the shim's new `QWE_OOM_SITE_LOG` on every injection test and mapped the failed call sites
back with `addr2line -i`. Coverage cannot answer this: the harness fails allocations in a forked
probe, which never flushes its coverage.

**Verdicts.** 1 = checked here, failure reported or handed to the parent. Under "reached by",
`V` = `src/cli/validate:oom_test` (valid, cycle and duplicate-key workflows), `R` =
`src/kernel:oom_test::run_survives_every_injection`, `S` = `::select_job_survives_every_injection`
(new: a workflow past `read_file`'s first buffer, run with `--job`), `K` = `src/kernel:sites_oom_test`
(new), `E` = `src/secrets:oom_test` (new), `C` = `src/cli/encrypt:oom_test` (new), `N` =
`src/cli/run:oom_test` (new).

| site | verdict | reached by |
|---|---|---|
| `jobs.c` 65 (jobs grow), 79 (id), 97 (target), 112 (needs), 118 (need) | 1: `goto nomem`, message, `qwe_jobs_free` | V, R |
| `dag.c` 91, 92 | 1: `QWE_DAG_NO_MEMORY`, reported as a problem | V |
| `validate.c` 28 (problems grow), 37 (message), 51 (locate), 131 (ids grow), 138 (id), 145 (jobs), 157 (needs), 164 (need), 180 (pointer), 215 (`strndup`, workflow dir) | 1: sets `ps->oom` or `goto nomem`; the caller prints the file and "out of memory" | V |
| `validate.c` 77 (`escape_token`), 262 (`last_token`) | 1: NULL, then `oom` | 77: V; 262: V (the duplicate-key scenario, new) |
| `validate.c` 177 (`strdup("")`, after the DAG check itself failed) | 1: `goto nomem` | **not reachable**: it runs only after an earlier allocation (`dag.c` 91/92) has already failed, and the shim fails exactly one. A second failure would be needed. The branch is the same `goto nomem` as the line below it |
| `workflow.c` 148, 151 (a step's argv, in the forked child) | 1: reports `engine-error` over the result pipe | R (the child's 1st, 2nd ... allocation) |
| `workflow.c` 206 (`read_file` grows), 228 (`mkdir_p`), 345 (result buffer grows: sets `res_overflow`), 424 (default inventory path), 1610 (`--job` selection), 1722 (`strndup`, run dir), 1724, 1736 | 1: message and `QWE_EXIT_USAGE`, or `res_overflow` | R, S (206, 1610 by S) |
| `workflow.c` 1508 (`group_names`) | **3, was unchecked**: NULL dereference on failure. Now `qwe_xcalloc` (rule 2: a failure while setting up the run loop has nowhere to go) | R: the abort names `workflow.c:1508` |
| `luaexec.c` 29 (capture buffer), 79, 84 (`qwe.exec.run` argv), 328, 331 (preamble) | 1: `luaL_error` "out of memory" or -1 | 29, 79, 84: K (new); 328, 331: R |
| `luafs.c` 36, 43 | 1: `goto nomem`, returns `nil, "out of memory"` (`plugins.lua` reports it, from quality/07) | V |
| `luacbor.c` 265, 283, 371 | 1: `*err = "out of memory"` | 265, 371: V, R; 283: K (new: a map of 20 keys, so the key list grows) |
| `redact.c` 67, 107, 127 | 1: -1 to the sink | K (new) |
| `envelope.c` 26, 27 (seal), 55 (`decode`), 103 (open) | 1: NULL / -1 with `why = "out of memory"` | E, C (new); 55: E |
| `positions.c` 19, 46, 54 | 1: -1, then the transcoder's `oom()` | V |
| `transcode.c` 71, 410, 419 | 1: `oom(c)` / "out of memory" | V |
| `transcode.c` 456 | 1: -1 | **not reachable**: `qwe_yaml_events_for_test` is a test-only entry point, never called with the shim armed |
| `preamble.c` 45 | 1: -1 | R |
| `sink.c` 28 | 1: -1, the run says it cannot open the log | R |
| `ring.c` 9 | 1: -1 | R |
| `encrypt.c` 17 | 1: -1, "cannot read stdin" | C (new) |
| `run.c` 18 | 1: "qwe run: out of memory", exit 2 | N (new) |
| `tools/bcembed.c` 39, 81, 72 (`strndup`) | **4, exempt**: build-time tool, run once by bazel over trusted inputs; a failure fails the build | n/a |

The "reached by" letters name the test that owns the site; the address log confirmed that every site except the two marked "not reachable" is failed by one of these tests, but I did not keep the per-test breakdown, so an attribution to V or R for a site the new tests do not own is my reading of where it runs.

Two kinds the ticket asked to read hardest:

- Allocations made while reporting some other error (`validate.c` 37, 51, 177, 180, 262): each sets `ps->oom`, which the caller turns into "file: out of memory". None is a silent skip. 177 is the only one no single-failure test reaches, for the reason above.
- `realloc` assigned back over the only pointer: none left. Every `realloc` goes to a `grown` (or `nn`/`nv`) temporary first. `luaexec.c` 328/331 frees both arrays if either fails.

Bugs found: one. `workflow.c` 1508 was unchecked (classified 3) and is converted. The new tests
(`sites_oom_test`, the `--job` case, `encrypt`, `run`, `secrets`) found nothing else once written; the
two suspected candidates, `luaexec.c` 29 and `luacbor.c` 283, both report "out of memory".

**One mistake to know about:** my first `src/secrets/oom_test.c` swept nothing. Its baseline ran with the shim disarmed, and a disarmed shim does not count, so `count` was 0 and the loop body never ran; the test passed with 6 assertions. It now counts with the shim armed past the end (as the other tests do) and asserts `count > 0`. Worth checking any new sweep for the same thing.

The check that keeps the count from creeping back: `src/kernel/alloc_audit_test.sh`
(`//src/kernel:alloc_audit_test`) counts the bare calls per file against
`src/kernel/alloc_audit.txt` and fails on any difference in either direction; `tools/bcembed.c` is
listed there as exempt with its reason. It fails on a new call added to `ring.c` (tried), and
`alloc.h` points to it. Each package's sources reach it through a `c_files` filegroup.

`QWE_OOM_SITE_LOG=<file>` (documented in `oom_shim.h`) records each failed call as an offset;
`addr2line -i -e <test> $(sed 's/^/0x/' <file>)` gives the call sites.

`bazel test //...`, `--config=ubsan` and `--config=asan` are green (194 pass, 2-3 skipped).
