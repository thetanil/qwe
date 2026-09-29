# 32: clang-tidy's evidence bundle on every exit, not only once the lint starts

Status: ready-for-agent
Category: bug
Type: task

## What

`tools/clang-tidy/run.sh --evidence-dir DIR` is documented to write its bundle "whether it
passes or fails" (`docs/static-analysis.md`, "Evidence"; the script's own header). It
writes it only at line 336, just before the lint loop. Every check before that point
`exit`s with no bundle at all:

| Line | Refusal |
|---|---|
| 117 | a `bazel` query, aquery or build failed (`bazel_out`) |
| 175 | `bazel aquery` found no `CppCompile` action |
| 192 | `clang-tidy` not found |
| 203 | `clang-tidy` is not the pinned major (`tools/clang-tidy/pin.env`) |
| 220 | no `genrule` under `//third_party/...` |
| 258 | `clang-extdef-mapping` not found |
| 265 | `clang-extdef-mapping` is not clang-tidy's major |
| 278 | `clang-extdef-mapping` failed on a file (the cross-translation-unit map) |

(Line numbers at `3a87980`.)

In CI the `static-analysis` job then uploads `$RUNNER_TEMP/clang-tidy-evidence` with
`if: always()` and `if-no-files-found: error`, so the upload step fails too, and the run
has no bundle to diagnose from. That is the case the pin exists for: a moved clang, or a
broken `.github/actions/clang-tidy-pin`, is exactly a line-203 or line-192 exit. On a
release, `release.yml` then has no `clang-tidy-evidence-<sha>` artifact to download.

Found in the Copilot review of PR #10
(<https://github.com/thetanil/qwe/pull/10#pullrequestreview-5346923387>): it flagged
the same gap in `tools/gcc-analyzer/run.sh`, which was fixed on that PR. This script was
not in the diff, so it was not flagged. `set -euo pipefail` is on here, so a failed
`mkdir`/`cp` into the evidence directory already fails the run; that half of the finding
does not apply.

## Fix

The shape `tools/gcc-analyzer/run.sh` has since PR #10:

- create the evidence directory and write what is known up front (`version.txt` as far
  as it can be read, `.clang-tidy`, `sha.txt`) before the first check;
- a `refuse` helper for the early exits that also appends the reason to `preflight.txt`;
- an `EXIT` trap that copies whatever the run got to (`files.txt`, `ctu-map.txt`,
  `output.txt`) and always writes `exit_status.txt`, turning a failed write into exit 2.

Keep `--list` and `--raw` as they are: neither takes `--evidence-dir`.

## Acceptance criteria

- [ ] With `CLANG_TIDY` pointing at a missing binary, `run.sh --evidence-dir DIR` exits 1
      and DIR holds `preflight.txt` naming the reason, `sha.txt`, `.clang-tidy` and
      `exit_status.txt` (`1`). `manual: CLANG_TIDY=/nonexistent tools/clang-tidy/run.sh --evidence-dir /tmp/ev`
- [ ] The same for a major-version mismatch (a wrapper script that prints another version).
      `manual`
- [ ] An evidence directory that cannot be created fails the run with exit 2 and says so.
      `manual: tools/clang-tidy/run.sh --evidence-dir /proc/nope`
- [ ] A passing run's bundle is unchanged (same file names, same content shape).
      `manual: diff the file list against the last CI artifact`
- [ ] `docs/static-analysis.md` ("Evidence") says which files are there on every exit and
      which only as far as the run got.
- [ ] `tools/clang-tidy/run.sh` exits 0 and `bazel test //...` is green.

## Comments
