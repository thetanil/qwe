# Smoke workflows

`tests/smoke/*.yml` are ordinary qwe workflows, run for real by `.github/workflows/smoke.yml`
against the actual release binary on a clean runner (not just inside the Bazel sandbox), and
also run against the fastbuild binary by `bazel test //tests/smoke:smoke_workflows_test` as
part of `bazel test //...`, so they cannot rot between the pushes that exercise them for real.
The design and its reasoning: `docs/ci-checks.md`'s Smoke row.

## Layout and naming

| File | What |
|---|---|
| `smoke_<area>.yml` | a positive workflow: runs, and is expected to succeed |
| `neg_<case>.yml`, or `neg_<case>/` with its own `w.yaml` | a negative: expected to fail, with a specific message |
| `<name>.yml.in` | a template, never run directly; a generator script fills it in (`smoke_secrets.yml.in` + `gen_secrets.sh` is the only one so far) |
| `.qwe/plugins/<name>/` | a project plugin fixture used by a smoke workflow (e.g. `smoke.touch`, used by `smoke_project_plugin.yml`) |

A workflow that needs something the Bazel sandbox cannot give (real root, passwordless
sudo, a real `apt-get install`) carries a comment near its top:

```yaml
# smoke: skip-in-bazel: needs real apt-get and root (become) that the sandbox does not have
```

`smoke_workflows_test.sh` skips it (and says so); it still runs for real in the smoke
GitHub job, which has no sandbox to worry about.

## Running one locally

Against the fastbuild binary, from a scratch directory (a workflow's `run:` steps write
into the current directory, and `qwe` writes `.qwe/runs/` there too):

```
bazel build //src/cli:qwe
mkdir /tmp/t && cd /tmp/t
cp /path/to/qwe/tests/smoke/smoke_file.yml .
/path/to/qwe/bazel-bin/src/cli/qwe validate smoke_file.yml
/path/to/qwe/bazel-bin/src/cli/qwe run smoke_file.yml --summary summary.md
```

A workflow with a project plugin fixture (`smoke_project_plugin.yml`) needs
`tests/smoke/.qwe/` copied alongside it too, since a project plugin loads from next to the
workflow file, not from the current directory. `smoke_secrets.yml` does not exist as a
file to copy: run `tests/smoke/gen_secrets.sh <qwe-binary> tests/smoke` instead, which
generates it from `smoke_secrets.yml.in`, runs it, and checks for a leak.

Or just run the whole suite the way CI does:

```
bazel test //tests/smoke:smoke_workflows_test --test_output=all
```

## The negative-case pattern

Two different things "negative" can mean here, and most negatives are only the first:

**GitHub-only** (needs real privilege, so the Bazel sandbox cannot check it): a
`continue-on-error: true` step runs `qwe validate` then `qwe run --debug`, `tee`ing the
output to a file; the very next step fails the job unless that step's own `outcome` was
`failure` and the file contains the expected message (`grep -F`). The same pair of steps
appears again, unprefixed by `--debug`, against the shipped release binary in the `smoke`
job. See `neg_file_ensure_denied.yml` and its two steps in `smoke.yml` for the template to
copy.

**Verifiable in Bazel too** (ticket 12's four: `neg_timeout.yml`, `neg_malformed.yml`,
`neg_dependency_failed.yml`, `neg_plugin_top_level/`): these need no privilege, so
`smoke_workflows_test.sh` runs them for real too. Carry a marker comment instead of
GitHub's continue-on-error dance:

```yaml
# smoke: expect-fail: <message>
```

`smoke_workflows_test.sh` runs `qwe validate`; if that fails, `<message>` must be
somewhere in its output. If validate passes, it runs `qwe run --summary` instead and
requires *that* to fail, with `<message>` in its output or its summary (a failure's
`reason` — `timeout`, `dependency-failed` — only ever shows up in the summary table, never
on stdout: check `tests/e2e/summary_timeout` or `summary_failure_skip` if in doubt). A
`neg_*/` case is a directory instead of a flat file only when it needs its own
`.qwe/plugins/` (a project-plugin negative); the marker goes in its `w.yaml`.

A workflow-that-needs-privilege negative (`neg_apt_unknown.yml`, say) is *not* given this
marker: the Bazel sandbox cannot make it fail for the right reason (no real apt, or no
real root to lack), so it stays GitHub-only.

## Keeping this from rotting

`tests/smoke:coverage_test` (`bazel test //...`) fails when:

- a built-in plugin (`src/kernel/lua/BUILD`'s `MODULES` table: every `plugin.<name>` and
  `backend.<name>`) is used by no smoke workflow (`uses: <name>`, `run:` for `run`,
  `target: local` for `backend.local`). `backend.ssh` (no ssh in smoke) and
  `backend.recording` (test-only) are exempt, with the reason next to each in
  `check_plugin_coverage` in `tests/smoke/coverage_test.sh`;
- a smoke workflow or negative that the generic Bazel run cannot itself verify (marked
  `skip-in-bazel`, or a `neg_*` with no `expect-fail` marker) is not named anywhere in
  `.github/workflows/smoke.yml`. A plain workflow or a marked negative needs no explicit
  mention there: `tests/smoke/BUILD`'s `glob(["*.yml"])` already puts it in front of the
  generic Bazel run, so it cannot go silently unused.

So: writing a new built-in plugin means adding it to a smoke workflow (see "Writing a
plugin" in the README), and adding a `skip-in-bazel` or unmarked-negative smoke file means
also adding the GitHub step that runs it.
