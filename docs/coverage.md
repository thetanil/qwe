# Coverage

Coverage is a diagnostic, not a target. It shows which branches no test reaches;
the useful question is whether each of them is an error path that should have a test.

```
bazel coverage //... --combined_report=lcov
lcov --summary bazel-out/_coverage/_coverage_report.dat
```

The combined report (`bazel-out/_coverage/_coverage_report.dat`, lcov format) covers the
unit tests **and** the e2e cases together. The e2e cases run the real `qwe` binary, and its
coverage is collected from the process, so they count: leaving them out would send the
work to code that is already exercised end to end. It takes about 35 s on a warm cache.

Per file, lines hit / lines found (`src/` only; vendored code is upstream's):

```
awk -F: '/^SF:/{f=$2} /^LF:/{lf[f]=$2} /^LH:/{lh[f]=$2} END{for(f in lf) if (f ~ /^src\//) printf "%-40s %d/%d\n", f, lh[f], lf[f]}' \
    bazel-out/_coverage/_coverage_report.dat | sort
```

The lines a file misses, for example `src/kernel/luafs.c`:

```
awk -v f=src/kernel/luafs.c '/^SF:/{on=($0=="SF:"f)} on&&/^DA:/{split(substr($0,4),a,","); if(a[2]==0) print a[1]}' \
    bazel-out/_coverage/_coverage_report.dat
```

Branch data is not produced by this toolchain (`lcov` reports "no data found"); lines and
functions are.

## Baseline (2026-09-20, before quality/05's tests)

Whole report: 74.2% of lines (3404 of 4587), 85.8% of functions (314 of 366), 139 test
targets plus 110 e2e cases run.

| file | lines hit / found |
|---|---|
| `src/cli/run/run.c` | 32 / 36 |
| `src/cli/validate/validate.c` | 8 / 16 |
| `src/kernel/sink.c` | 49 / 53 |
| `src/kernel/result.c` | 62 / 78 |
| `src/kernel/trace.c` | 34 / 44 |
| `src/kernel/jobs.c` | 111 / 121 |
| `src/kernel/validate.c` | 204 / 248 |
| `src/kernel/luaexec.c` | 193 / 268 |
| `src/kernel/luafs.c` | 51 / 71 |
| `src/kernel/luasecrets.c` | 39 / 45 |
| `src/cli/encrypt/encrypt.c` | 5 / 50 |
| `src/kernel/proc.c` | 34 / 59 |
| `src/kernel/workflow.c` | 979 / 1161 |

## After quality/05

Whole report: 75.5% of lines (3463 of 4588).

| file | lines hit / found |
|---|---|
| `src/cli/run/run.c` | 34 / 36 |
| `src/cli/validate/validate.c` | 16 / 16 |
| `src/kernel/sink.c` | 51 / 53 |
| `src/kernel/result.c` | 78 / 78 |
| `src/kernel/trace.c` | 36 / 45 |
| `src/kernel/jobs.c` | 111 / 121 |
| `src/kernel/validate.c` | 204 / 248 |
| `src/kernel/luaexec.c` | 206 / 268 |
| `src/kernel/luafs.c` | 61 / 71 |
| `src/kernel/luasecrets.c` | 42 / 45 |

Still open, and not in the ticket's table: `src/cli/encrypt/encrypt.c` (5/50) and
`src/kernel/proc.c` (34/59) are the two largest gaps left in `src/`.
