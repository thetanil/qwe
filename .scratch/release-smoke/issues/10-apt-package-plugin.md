# 10: apt.package plugin and smoke_apt.yml

Status: ready-for-agent
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

- [ ] present: not installed → install; installed → nothing; absent: installed → remove; not installed → nothing: `plugin: plugins/builtin/apt.package/test.lua::four_states` (recording backend, scripted `apt list` output)
- [ ] Unknown package, missing root, apt-get failure, not-converged messages: `plugin: plugins/builtin/apt.package/test.lua::errors`
- [ ] A name with shell metacharacters is refused at validate: `e2e: tests/e2e/validate_apt_package_name/`
- [ ] `smoke_apt.yml` passes on the runner, and the summary shows changed/unchanged for each apt step: `manual: push; paste the summary's apt table into a comment`
- [ ] Both negatives fail with their messages, and the job stays green: `manual: same run`
- [ ] `bazel test //...` green; the coverage floor holds

## Comments
