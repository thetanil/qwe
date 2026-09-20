# Valgrind gate

Valgrind checks the shipping build: no rebuild with instrumentation, so it is the
artefact that gets checked, not a variant of it. It finds what ASan does not
model, chiefly branches and syscalls that depend on uninitialised memory.

```
bazel test --config=valgrind //...           # every cc_test under valgrind
bazel test //tests/e2e:valgrind_e2e          # six e2e cases, qwe and its step children traced
```

## The two pieces

**Every `cc_test`.** `--config=valgrind` sets `--run_under=//tools/valgrind:run_under`.
`run_under.sh` runs a compiled test as `valgrind --leak-check=full
--error-exitcode=1` (leaks that are definite or indirect count) and runs a shell test
(every e2e case, `luarun_test`) as it is, since valgrind on a shell would trace the
shell. The e2e cases of the plain suite are therefore unchanged under the config.

**A named subset of e2e cases,** `//tests/e2e:valgrind_e2e`, chosen for the paths
the unit tests cannot reach:

| case | path |
|---|---|
| `run_outputs` | a plain run |
| `file_ensure_idempotent` | a run with a plugin |
| `secret_redacted_env` | secrets and redaction |
| `operator_cancel` | cancellation and teardown |
| `step_timeout_fails` | a timeout |
| `needs_skip_chain` | a `needs:` chain |

The ssh cases are left out: they need a reachable host and already skip. The
targets are `<case>_valgrind_test`, tagged `manual` so `//...` does not build
them; add a case in `tests/e2e/BUILD` in the list next to `valgrind_e2e`.

## Which processes are traced

qwe forks a child per step. The child runs Lua (`child_argv` in `workflow.c`) and
then `execvp`s the step's command. Valgrind follows the fork, so that Lua is
covered. `--trace-children-skip='/bin/*,/usr/*,/sbin/*,/lib/*'` stops it following
the exec into `sh`, `ssh`, `sleep` and whatever else a workflow runs; qwe itself
lives under the Bazel output tree and is not matched. Reports go to one log per
traced process in the test's undeclared outputs (`valgrind-<case>/valgrind.<pid>`),
never stderr, because the goldens compare stderr. `valgrind_case.sh` fails the test
if any log is non-empty (`--quiet`, so an empty log means nothing to report), if the
case itself fails, or if no log exists at all. Checking the logs, not only the
exit code, matters because a signalled or exec'd step child never returns its
`--error-exitcode`.

Checked by hand on `run_outputs`: three traced processes (qwe and its two step
children, all showing `Command: .../qwe run w.yaml`), none of `sh`.

## Suppressions

`tools/valgrind/luajit.supp` has **no entries**. LuaJIT was expected to need some
(its allocator and stack handling), but the whole suite is clean under valgrind 3.22
without any. The file is kept, with its rule stated at the top, so that a needed
suppression has a place: comment each entry with what it hides and why that is not
a real finding.

## Liveness

`//src/kernel:valgrind_smoke_test` is a test only under `--config=valgrind` (skipped
otherwise). Its child branches on an uninitialised value; the test passes only if
valgrind makes that child exit non-zero. Run without valgrind it fails ("the gate
is not live").

## Timing and cadence

Measured on the 16-core devcontainer, valgrind 3.22:

| piece | wall clock | note |
|---|---|---|
| plain `bazel test //src/...` (tests only, built) | about 3 s | baseline |
| `--config=valgrind`, every `cc_test` | about 130 s | 128.8 s of it is `load_oom_test`; the rest each take under 5 s |
| `valgrind_e2e`, six cases in parallel | about 6 s | about 5.5 s each |

`load_oom_test` fails every allocation on the load path in turn and starts a
LuaJIT VM for each, which is 0.5 s plain and a hundred times that traced. It is the
test most likely to reach an error path with a half-built object, so it stays in
the gate.

Cadence, from those numbers rather than a guess: both pieces are cheap enough for
every change that touches C; if the 130 s is too long for that, run the whole
config nightly and keep `--config=valgrind //src/... -- -//src/kernel:load_oom_test`
(about 6 s) per change. The e2e piece at 6 s needs no slower cadence than the
unit piece. There is no CI service in this repo yet.

Valgrind and ASan do not combine; use one config at a time.
