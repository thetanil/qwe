# Issue tracker: Local Markdown

Issues and specs (you may know a spec as a PRD) for this repo live as markdown files in `.scratch/`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The spec is `.scratch/<feature-slug>/spec.md`
- Implementation issues are one file per ticket at `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01` — never a single combined tickets file
- Triage state is recorded as a `Status:` line near the top of each issue file (see `triage-labels.md` for the role strings)
- Comments and conversation history append to the bottom of the file under a `## Comments` heading
- A closed feature moves to `.scratch/archive/<feature-slug>/`, which the frontier glob does not reach. Promote its still-load-bearing decisions to `docs/adr/` or `CONTEXT.md` first — the archived tickets are the record of how the project got somewhere, not the statement of where it is. See `.scratch/archive/README.md`

## Closing a feature

A feature is closed when every ticket under `.scratch/<feature-slug>/issues/` has `Status: resolved` (or `wontfix`, with the reason in its comments), and `bazel test //...` is green. Then, in this order:

1. **Promote what is still load-bearing.** For each decision in the tickets' `## Comments` that a later reader needs as a rule, not as history, write it where an agent reads: `docs/adr/` for a decision with rejected alternatives, `CONTEXT.md` for a term, the design docs or a `docs/<topic>.md` for how a check or a subsystem works. Most of it is usually there already, written as each ticket closed; the promotion is the last sweep for what was not.
2. **Repoint live references.** `grep -rn ".scratch/<feature-slug>"` outside `.scratch/`: a comment in code or a doc that names a ticket must name the archived path (`.scratch/archive/<feature-slug>/...`) after the move.
3. **Move it.** `git mv .scratch/<feature-slug> .scratch/archive/<feature-slug>`, keeping the layout. Do not edit the tickets afterwards; a wrong or unfinished one gets a new ticket in a live feature.
4. **Record it.** Add a row to the table in `.scratch/archive/README.md` (feature, date closed, what it built, ticket and criterion counts) and one paragraph on what was promoted and where.
5. One commit for the move, so the history shows the feature closing as one step.

## Acceptance criteria

Every implementation issue has an `## Acceptance criteria` section written as checkboxes.

- Every item names the test that proves it, using one of these forms:
  - `unit: src/kernel/ring_test.c::drop_accounting`: a greatest test in a `cc_test`
  - `plugin: plugins/builtin/<name>/test.lua::<case>`: a plugin test against the recording backend
  - `e2e: tests/e2e/<case>/`: the real `qwe` binary running a workflow, checked against golden files
  - `manual: <where>`: only when no automated test is possible, with the steps written out
- If an item can't name a test, rewrite it until it can, or mark it `manual:` and write out its steps.
- An issue is done only when every automated item's test exists and passes under `bazel test //...`.

## When a skill says "publish to the issue tracker"

Create a new file under `.scratch/<feature-slug>/` (creating the directory if needed).

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. The user will normally pass the path or the issue number directly.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a file with one **child** file per ticket.

- **Map**: `.scratch/<effort>/map.md` — the Notes / Decisions-so-far / Fog body.
- **Child ticket**: `.scratch/<effort>/issues/NN-<slug>.md`, numbered from `01`, with the question in the body. A `Type:` line records the ticket type (`research`/`prototype`/`grilling`/`task`); a `Status:` line records `claimed`/`resolved`.
- **Blocking**: a `Blocked by: NN, NN` line near the top. A ticket is unblocked when every file it lists is `resolved`.
- **Frontier**: scan `.scratch/<effort>/issues/` for files that are open, unblocked, and unclaimed; first by number wins.
- **Claim**: set `Status: claimed` and save before any work.
- **Resolve**: append the answer under an `## Answer` heading, set `Status: resolved`, then append a context pointer (gist + link) to the map's Decisions-so-far in `map.md`.
