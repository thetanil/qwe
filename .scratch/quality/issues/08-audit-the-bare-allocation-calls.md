# 08: Audit the bare allocation calls

Status: ready-for-agent
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

- [ ] Every remaining bare call is listed in this ticket's comments with its verdict (1 to 4 above), and no site is left unclassified.
- [ ] Every site classified 2 or 3 is converted or checked, and each has an OOM-injection case from ticket 07. `unit: the injection cases named per site in the comments`
- [ ] Every site classified 1 is confirmed reached by the injection harness or by an existing failure test; a site that no test can reach is noted with why.
- [ ] A check keeps the count from creeping back: either the grep above is a test that compares against an allowlist of exempt files, or the exemption list is written next to the helper. `unit or script: fails when a new bare call appears outside the allowlist`
- [ ] `bazel test //...`, `--config=ubsan` and `--config=asan` are green.

## Comments
