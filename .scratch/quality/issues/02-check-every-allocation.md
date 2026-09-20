# 02: Check every allocation, and fail cleanly when one fails

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

There are roughly fifty `malloc`, `calloc`, `realloc` and `strdup` calls
outside the test files, and most do not check the result. Under memory
pressure they are NULL dereferences rather than clean failures, which for a
tool heading for certification is the wrong answer twice over: it crashes, and
it crashes in a way that says nothing about why.

The pattern repeats across the tree. A representative sample:

| Site | Shape |
|---|---|
| `src/kernel/luafs.c:33` | `names = realloc(...)` then `names[n++] = strdup(...)` on the next line, neither checked |
| `src/kernel/jobs.c:50` | `jobs = realloc(jobs, ...)` then written through immediately |
| `src/kernel/sink.c:27` | `malloc` checked, but `emit_line`'s `malloc` silently drops a log line when it fails |
| `src/kernel/validate.c:25,57,105` | the error-collection path allocates freely while reporting someone else's error |
| `src/kernel/workflow.c:375` | `default_path = malloc(...)` then `memcpy` into it |
| `src/edge/yaml/positions.c` | checked and returns -1 — the pattern to copy |

Some places already do it properly: `qwe_ring_init` returns -1, the transcoder
has a dedicated `oom()` that produces a positioned error, `positions.c` returns
-1 and the caller turns it into `oom`. The work is to make the rest look like
those, not to invent an approach.

**Three rules to settle first**, because "check everything" without them turns
into fifty ad-hoc decisions:

1. **Where a failure can be reported, report it.** Anything on the load,
   validate or job-setup path has a caller that can print a message and exit —
   those become real error returns.
2. **Where it cannot, say so and abort.** The output path is the awkward one:
   `emit_line` failing to allocate means a log line is lost, and the log is the
   source of truth (§10). Losing it silently is worse than stopping. A single
   `qwe_xmalloc` that prints and aborts is an honest answer for those, and it
   is far better than a NULL deref because it names the allocation.
3. **The child after `fork` is its own case.** An allocation failure there
   should reach the parent as a step result on the result pipe, not just a
   dead child, so the step gets a reason instead of a bare non-zero exit.

Note that one allocation failure changes an *answer* rather than crashing —
`qwe_dag_check` returning `QWE_DAG_OK` when its `calloc` fails. That is a
correctness bug, not hardening, and it is `m1-review/07`.

## Acceptance criteria

- [x] Every allocation outside the test files is either checked at its call site or made through an aborting helper; a grep for the bare pattern returns only the helper's own definition. `unit: src/kernel/alloc_test.c::helper_aborts_and_names_the_site`
- [x] The three rules above are written down where the helper is defined, so the next allocation added picks the right one without re-deriving it.
- [x] An allocation failure on the load path produces a message naming the file and exits 2, rather than faulting. `unit: src/edge/yaml/alloc_test.c` (existing pattern, extend to the new sites)
- [x] An allocation failure in the forked child reaches the parent as a step result with a reason, and the step is `failed` rather than mysteriously dead. `unit: src/kernel/workflow_test.c::child_oom_reports_a_reason` (or an e2e case under a memory limit, if one can be made reliable)
- [x] The `ulimit` e2e mechanism is reused rather than reinvented where a real failure can be provoked. `e2e: tests/e2e/engine_error_spawn/` (existing, must stay green)
- [x] ASan and UBSan runs of the full suite show no new findings attributable to the change. `manual: run the suite under the configs from ticket 03`

## Comments

Pairs with ticket 05's OOM-injection harness: once the checks exist, injection
is what proves they are on the right lines. Doing them in the other order gives
a harness that only ever finds the same NULL deref.

### Status: blocked by 03

- `src/kernel/alloc.{h,c}`: `qwe_xmalloc/xcalloc/xrealloc/xstrdup` print
  `file:line: out of memory (N bytes)` and abort. The three rules are the header
  comment. Test: `alloc_test::helper_aborts_and_names_the_site`.
- Used only where nothing can report: sink lines, the redaction set (a secret
  that is not recorded is not masked), and parent-side mid-run state in
  `workflow.c` (`step_target`, `step_json`, `steps`, disabled notes, `run_all`'s
  tables, results).
- Load path: `jobs.c`, `validate.c`, `workflow.c` (`read_file` now sets ENOMEM,
  so it no longer prints a stale errno), `luafs.c`, `luacbor.c`, `run.c` are
  checked and report. The transcoder reported libyaml's own allocation failure
  as "invalid YAML"; it now says out of memory.
- Test: `load_oom_test` fails every allocation, in turn, on `qwe validate`'s
  path (valid and invalid workflow): exit 2, a message naming the file and
  memory, no fault. `jobs_test::jobs_load_fails_cleanly_when_any_allocation_fails`
  does the same for `qwe_jobs_load` and checks nothing leaks.
- Child: `child_argv` builds argv before sending `exec`; on failure it sends
  `failed` / `engine-error`, which the parent now accepts as a reason. Test:
  `workflow_test::child_oom_reports_a_reason` (found in result.json).
- `bazel test //...` is green (168 tests).
- Not done: the MSan/ASan run (ticket 03). Left open for that.

### What this waits on

Implementation is done and `bazel test //...` is green (168 tests). Only the
last acceptance criterion is open:

- [x] ASan and UBSan runs of the full suite show no new findings attributable
  to the change.

That needs the sanitizer configs from ticket 03 (`03-sanitizer-builds.md`),
which do not exist yet. Nothing else is outstanding.

When 03 lands: run `bazel test //...` under the ASan and MSan configs, fix or
explain any finding in the files this ticket touched (`alloc.c`, `jobs.c`,
`validate.c`, `workflow.c`, `sink.c`, `redact.c`, `luafs.c`, `luacbor.c`,
`luaexec.c`, `run.c`, `transcode.c`). Note that `load_oom_test`, `jobs_test`,
`alloc_test` and `workflow_test` use `-Wl,--wrap`, which some sanitizers
intercept themselves, so they may need excluding or adjusting under those
configs. Then tick the criterion, set `Status: resolved`, and commit
(one commit for the ticket, per the project rule).

The working-tree changes are uncommitted until then.

### Resolved

Ticket 03 landed. `bazel test //...`, `--config=ubsan` and `--config=asan` all pass on the full tree (171 targets), with no findings attributable to this change. The `--wrap` tests (`alloc_test`, `load_oom_test`, `jobs_test`, `workflow_test`) run unchanged under ASan. MSan was dropped in 03 (LuaJIT makes it unusable, see `docs/sanitizers.md`), so the criterion is met with ASan and UBSan.
