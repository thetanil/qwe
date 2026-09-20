# 01: Duplicate YAML keys are accepted, and the last one silently wins

Status: ready-for-agent
Category: bug
Type: task
Blocked by: none

## What

Nothing rejects a repeated key in a YAML mapping. libyaml emits both key/value
pairs, the transcoder encodes both into the CBOR map, and `qwe_cbor_to_lua`
builds a Lua table where the later assignment overwrites the earlier one. The
first value is gone before validation ever sees the document.

Checked on 2026-09-20 with this workflow:

```yaml
jobs:
  j:
    target: local
    steps:
      - run: echo first
jobs:
  k:
    target: local
    steps:
      - run: echo second
```

```
$ qwe run d.yaml
[k] second
rc=0
```

Job `j` vanished. No error, no warning, exit 0. The same holds at any depth:
two `run:` keys in one step, two `target:` keys in one job, two entries of the
same name in a `secrets:` or `vars:` map.

This is how an operator loses work by accident — merging two workflow
fragments, or pasting a block twice and editing only one copy. GitHub Actions
rejects duplicate keys. Design §15.4 rejects anchors, aliases and merge keys
"as in GitHub Actions" but is silent on duplicates, which is the gap.

**Where the fix goes.** The transcoder already records a JSON Pointer for every
node (ADR-0008), and two entries of the same mapping key produce the *same*
pointer. `qwe_positions_finish` sorts the table, which puts equal pointers
adjacent, so one pass over the sorted entries finds every duplicate with both
positions in hand. Report it the way every other document error is reported:
`file:line:col`, pointing at the second occurrence of the key, and naming the
line the first one is on.

An array index is not a duplicate: `/jobs/j/steps/0` and `/jobs/j/steps/1`
differ. Only a repeated map key collides, which is exactly what should be
caught.

## Acceptance criteria

- [ ] Two `jobs:` keys in one workflow are a validation error at the second key, and the message names the line of the first. `qwe validate` and `qwe run` both exit 2 and `qwe run` creates no run directory. `e2e: tests/e2e/validate_duplicate_key/`
- [ ] A duplicate key nested in a step (two `run:` keys in one step) is reported at its own position, not at the document root. `e2e: tests/e2e/validate_duplicate_key_nested/`
- [ ] A duplicate name in an inventory `secrets:` map is reported against the inventory file, with the inventory's own path in the error. `e2e: tests/e2e/inventory_duplicate_key/`
- [ ] Every duplicate in a document is reported, not only the first, consistent with the other validation errors. `e2e: tests/e2e/validate_duplicate_key/`
- [ ] Repeated sequence entries are not affected: a workflow with two identical `run:` steps in a list still runs both. `e2e: tests/e2e/steps_in_order/` (existing, must stay green)
- [ ] The duplicate check is in the transcoder's position table, so it costs one pass and no second parse. `unit: src/edge/yaml/positions_test.c::duplicate_pointers_are_found`
- [ ] Design §15.4 says duplicate mapping keys are rejected, next to anchors and aliases.

## Comments
