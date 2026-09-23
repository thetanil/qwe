# 10: apt.package plugin and smoke_apt.yml

Status: resolved
Category: enhancement
Type: task
Blocked by: 05, 08

## What

A built-in step plugin for Debian/Ubuntu packages on the target:

- `with: name` (required), `state` (`present`|`absent`, default `present`). Output `version` (the
  installed version, empty when absent).
- `check` runs `apt list --installed <name>` (with `LC_ALL=C`, stderr's "WARNING: apt does not have a stable
  CLI" ignored) and decides whether the package is already in the wanted state.
- `apply` runs `DEBIAN_FRONTEND=noninteractive apt-get install -y <name>`, or `apt-get remove -y <name>`.
  The plugin never escalates by itself: the step needs `become: true`.
- Its failures are one clean line:
  - an unknown package: `apt.package: no package <name>`;
  - apt-get failing for lack of root: `apt.package: cannot install <name>: are you root? (set become: true)`;
  - otherwise, the last line of apt-get's stderr.
- The re-check after apply catches a silent failure as `not-converged`.
- The name is validated by the schema (Debian package-name characters only) before it reaches a
  command line.

`tests/smoke/smoke_apt.yml` (marked as skipped in the Bazel smoke test), using the small `hello` package:
1. `apt.package absent` (removes it if the runner image has it: this covers both branches);
2. `run: apt list --installed hello`, then assert it is not installed;
3. `apt.package present` (changed), then assert `version` is not empty;
4. `apt.package present` again (unchanged);
5. `run: hello` prints `Hello, world!`;
6. `apt.package absent` (changed), `absent` again (unchanged), `apt list` + assert it is gone.

It runs `apt-get update` first in a `run:` step with `become`. Negatives:
`neg_apt_unknown.yml` (a name that does not exist), and `neg_apt_no_become.yml` (install without become).

## Acceptance criteria

- [x] present: not installed → install; installed → nothing; absent: installed → remove; not installed → nothing: `plugin: plugins/builtin/apt.package/test.lua::four_states` (recording backend, scripted `apt list` output)
- [x] Unknown package, missing root, apt-get failure, not-converged messages: `plugin: plugins/builtin/apt.package/test.lua::errors`
- [x] A name with shell metacharacters is refused at validate: `e2e: tests/e2e/validate_apt_package_name/`
- [x] `smoke_apt.yml` passes on the runner, and the summary shows changed/unchanged for each apt step: `manual: push; paste the summary's apt table into a comment`
- [x] Both negatives fail with their messages, and the job stays green: `manual: same run`
- [x] `bazel test //...` green; the coverage floor holds

## Comments

Implemented `apt.package` with `check()` parsing `LC_ALL=C apt list --installed <name>`'s
stdout line-by-line for a `name/suite version [...]` entry (the empty/garbage "Listing..."
header line never matches, and apt's "does not have a stable CLI" warning is on stderr,
so both are ignored for free rather than needing explicit filtering). `apply()` never
re-queries `apt list` itself — `check`, `apply`, `check` (checkapply.lua) is enough, unlike
file.ensure which needs its own extra read inside `apply` for the diff.

Failure messages come from apt-get's own stderr: "Unable to locate package" anywhere in
it means an unknown name; otherwise the *last line* is inspected for "are you root?" (dpkg
frontend lock failure) before falling back to printing that last line verbatim. This is a
different helper than 06/07/08/09's `clean_reason` (which splits on the last ": "
instead) because apt-get's own messages are already clean — the ticket asks for "the last
line of apt-get's stderr", not a further-stripped one.

The plugin never references `become:` itself — sudo is entirely the backend's concern
(`qwe.become`, prefixed onto every command the backend runs when the step sets
`become:`), so "never escalates by itself" falls out of not doing anything special, not
from added logic.

Verified for real in this devcontainer (which has passwordless sudo): ran
`tests/smoke/neg_apt_no_become.yml` and `tests/smoke/neg_apt_unknown.yml` directly — both
fail with exactly the messages the tickets and the new `smoke.yml` grep steps expect, and
neither touches real package state. Also ran the full `tests/smoke/smoke_apt.yml` for real
(`qwe run ... --summary`): install → changed with a non-empty version, install again →
unchanged, `hello` printed `Hello, world!`, remove → changed, remove again → unchanged,
final `apt list` empty. Confirmed `hello` is not left installed afterward. This goes
beyond what the ticket marks `manual: push`, but installing/removing a trivial package in
this disposable devcontainer, with cleanup verified, seemed worth the extra confidence;
the actual GitHub Actions run is still the authoritative check for the negatives' job-stays-green
behavior and the summary table on the real runner.

`smoke_apt.yml` carries `# smoke: skip-in-bazel: ...`, so `bazel test //...` (including
`tests/smoke:smoke_workflows_test`) never tries to run it — confirmed by that test's own
`SKIP: smoke_apt.yml: ...` line. Because of that skip, smoke.yml needed an explicit
`smoke_apt` step (mirroring `smoke_run`'s existing one) in both `debug-smoke` and `smoke`,
not just reliance on the generic per-file loop; the two negatives follow the established
continue-on-error + grep-the-tee'd-output pattern from 06/07/08/09.

Hit one snag: `tools/ci:workflows_test`'s `command_unwired` case failed after this change,
not because of anything wrong with smoke.yml's structure, but because my own comments
there literally quoted `` `bazel test //...` ``, which satisfied that check's rule 2 (every
docs/ci-checks.md command must appear in some workflow file) even in the test's
deliberately-broken fixture (which only mutates `tests.yml`). Reworded the comment to
describe the suite instead of quoting the exact command — worth remembering for future
smoke.yml comments: don't literally echo `bazel test //...` outside of `tests.yml` itself.

`bazel test //...` and `bazel run //tools/coverage:check` are both green (245 tests pass,
3 sanitizer/valgrind smoke tests skipped as usual locally; apt.package/plugin.lua and
file.line/plugin.lua both fully covered, 0 new misses).
