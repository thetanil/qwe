# 07: Publish the coverage report and a percentage badge on GitHub Pages

Status: in-progress (manual criteria await a push)
Category: enhancement
Type: task
Blocked by: 06

## What

Ticket 06's badge is pass/fail against the floor. That is the gate, but it does
not say how much is covered. This ticket publishes the number and the report on
each green push to `main`:

- A second job in `coverage.yml`, `needs:` the check, only on
  `refs/heads/main`, with `permissions: pages: write, id-token: write`. It
  deploys `coverage-html/` with `actions/upload-pages-artifact` and
  `actions/deploy-pages`. Pages must be set to "GitHub Actions" as the source in
  the repo settings. That is a manual step for the user.
- Next to the report, write `coverage.json`, a shields.io endpoint document
  (`{"schemaVersion":1,"label":"coverage","message":"<n>%","color":…}`), computed
  from `lcov --summary` line coverage for `src/` and `plugins/` only, not
  `third_party/`. Colour thresholds: red below 70, yellow below 85, green
  otherwise.
- The README gets a second coverage badge,
  `https://img.shields.io/endpoint?url=https://thetanil.com/qwe/coverage.json`,
  linked to the report.

Computing the percentage is a script (`tools/coverage/badge.sh <lcov file>`), so it
can be tested without CI.

`workflows_test` rule 1 matches badges to workflow files. This badge is not a
workflow badge, so the rule must not treat it as a dangling one.

Triage: this is the spec's first open question. Mark it `wontfix` if the
pass/fail badge is enough.

## Acceptance criteria

- [x] `badge.sh` on a fixture lcov file writes the expected percentage and colour, and counts only `src/` and `plugins/` records. `unit: tools/coverage/badge_test.sh::src_and_plugins_only`
- [x] It picks each colour at its boundary. `unit: tools/coverage/badge_test.sh::thresholds`
- [x] `workflows_test` ignores non-workflow badges. `unit: tools/ci/workflows_test.sh::non_workflow_badge`
- [ ] A green push to `main` deploys, and `https://thetanil.com/qwe/` shows the report. `manual: enable Pages (Settings → Pages → GitHub Actions), push, open the URL`
- [ ] The percentage badge renders with the same number as the job summary. `manual: view README on github.com`

## Comments

- Triage: not wontfix; the user asked for it and enabled Pages with the "GitHub Actions" source.
- `coverage.yml` gets a `pages` job (`needs: coverage`, only on `push` to `refs/heads/main`, `pages: write` and `id-token: write`, environment `github-pages`). The `coverage` job writes `coverage-html/coverage.json` with `tools/coverage/badge.sh` and prints it in the job summary; the `coverage-html` artifact is what gets deployed.
- `nightly.yml` and `release.yml` call `coverage.yml`, and a called workflow's jobs cannot ask for more permissions than the calling job has, even for a job that is skipped. Their `coverage` call jobs therefore carry `pages: write` and `id-token: write` (only those jobs).
- The README's second coverage badge is a shields.io endpoint badge, not a workflow badge; `workflows_test` ignores it (`non_workflow_badge`). Local check: `badge.sh` on the current report prints 93.0% green.
- First green push (46eee75, run 35660042207): coverage and pages jobs green; the report and coverage.json are served. `thetanil.github.io/qwe/` answers 301 to `https://thetanil.com/qwe/` (the account's Pages custom domain), so the README badge and docs use that URL directly. coverage.json says 93.0%, green.

- Vendored code (tinycbor, the only third_party record in the report) is dropped from the HTML report and the CI job-summary totals by `tools/coverage/ours.sh` (tested by `//tools/coverage:ours_test`); the badge and the floor already counted `src/` and `plugins/` only.
