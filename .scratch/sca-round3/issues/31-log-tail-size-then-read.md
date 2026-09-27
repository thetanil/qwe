# 31: `summary.c`'s `log_tail` sizes a file, then reads it

Status: ready-for-agent
Category: bug
Type: task

## What

`log_tail` (`src/kernel/summary.c:103`) sizes the job log with `fseek(SEEK_END)`/`ftell`,
allocates that many bytes, rewinds, and reads them back, to print the log's last lines in
the run summary. Two problems:

- **CERT FIO19-C / FIO45-C.** Sizing a file with `fseek`/`ftell` and then reading that many
  bytes is a check-then-use race: a file that changes between the two reads short (the
  step fails on a length mismatch) or leaves the tail out. `tools/bcembed.c`'s `slurp` had
  the same shape and was rewritten to read to end of file (2026-09-27): no size is taken
  first, and a short read ends the loop without touching the stream again. The clang
  analyzer's `unix.Stream` checker flagged the first version of that loop for reading a
  stream already at EOF, the same class as ticket 19.
- **Unbounded.** The whole log is read into memory to show its last few lines. A job that
  logs gigabytes makes the summary allocate gigabytes.

## Fix

Read only what the tail needs, and never trust a size taken earlier. Either read forward
through the file keeping the last N lines (a bounded ring of line starts over a bounded
buffer), or read backward from the end in fixed-size blocks until N newlines are found. The
backward read uses `fseek` to position, not to size an allocation, and it treats a short
read as the end. Cap what is kept (say 64 KiB), and mark the tail as cut when the cap is hit.

## Acceptance criteria

- [ ] No `ftell`/`SEEK_END` sizing in `src/`. `manual: grep -rn "ftell\|SEEK_END" src`
- [ ] A log larger than the cap produces a bounded tail with its last lines intact.
      `unit: src/kernel/summary_test.c`
- [ ] A log that is empty, has no trailing newline, or has fewer lines than asked for
      gives the same summary as today. `unit: src/kernel/summary_test.c`
- [ ] The gate exits 0 (no `unix.Stream` finding). `manual: tools/clang-tidy/run.sh`
- [ ] `bazel test //...` and the coverage check are green.

## Comments
