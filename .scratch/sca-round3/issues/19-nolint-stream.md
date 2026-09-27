# 19: The `unix.Stream` `NOLINT`

Status: ready-for-agent
Category: enhancement
Type: task

## What

`src/kernel/workflow.c:220`:

```c
	/* A failed fread (got <= 0) ends the loop via this same condition; fp is
	 * never read again, only fclose'd below, which is well-defined on a
	 * stream in any state. */
	// NOLINTNEXTLINE(clang-analyzer-unix.Stream)
	while (buf && (got = fread(buf + n, 1, cap - n, fp)) > 0) {
```

The analyzer thinks `fp` may be used after `fread` reported an error. The comment
argues it is not. A loop condition that assigns, reads and tests in one
expression is also the shape of `bugprone-assignment-in-if-condition` (ticket 07
of round 2 removed seven of those), and the reason the analyzer is confused.

## Fix

Rewrite `read_file` so the loop is plain and the error is explicit:

```c
for (;;) {
	if (!buf) break;
	got = fread(buf + n, 1, cap - n, fp);
	if (got == 0) break;
	n += got;
	...
}
failed = ferror(fp);
```

then decide what a read error means (it is not "end of file": the current code
treats a failed `fread` and EOF the same). Return -1 on `ferror`. That is a
behaviour change (a read error on a workflow file is reported, not treated as a
short file) and gets a test with a stream that fails its read (`fopencookie`
with a failing `read` callback, as `summary_test`'s
`mid_stream_write_failure_fails` does for writes: `read_file` takes a path today,
so split out a `read_stream(FILE *)` as ticket 03 of round 2 split
`qwe_summary_render`).

## Acceptance criteria

- [ ] No `NOLINT` in `workflow.c`. `manual: grep -n NOLINT src/kernel/workflow.c`
- [ ] A read error on a workflow file is reported. `unit: workflow_test.c` (a case
      whose reader fails halfway; red first)
- [ ] The gate exits 0. `manual: run.sh`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
