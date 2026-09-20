# 08: The transcoder trusts libyaml to balance its container events

Status: ready-for-agent
Category: bug
Type: task
Blocked by: none

## What

`handle_event` in `src/edge/yaml/transcode.c` closes a container like this:

```c
case YAML_MAPPING_END_EVENT:
case YAML_SEQUENCE_END_EVENT:
        c->depth--;
        rc = cbor_rc(cbor_encoder_close_container(&c->stack[c->depth], &c->stack[c->depth + 1]));
```

There is no check that `c->depth` is above zero. One end event more than there
were start events makes `depth` negative, and the next line reads
`c->stack[-1]` — a `CborEncoder` read from before the start of the array, and
then written through.

libyaml does balance its events for any stream it parses successfully, so this
is not reachable today. That is exactly why it should be checked anyway. §15.2
and §15.3 say libyaml is kept at the edge rather than trusted in the core,
because it is YAML 1.1, effectively in maintenance mode, and has a history of
CVEs on attacker-supplied files. A component that is quarantined because it may
misbehave, and then relied upon for a memory-safety invariant, is not
quarantined. The transcoder already enforces its own depth *ceiling* for the
same reason; the floor is the other half of the same check.

The cost is one comparison per container end, on a path that runs once per node
of a document that is at most 1 MiB.

While in the same function, two neighbours are worth a look:

- `scalar()` reaches `cbor_encode_tag(enc, QWE_SECRET_TAG)` under `if (tag)`,
  relying on `check_props` having already rejected every tag that is not
  `!encrypted`. That is true today and is not obvious from reading `scalar()`
  alone. Either assert it or compare the tag there.
- `scalar()` uses `strcmp` on `ev->data.scalar.value` for `~`, `null`, `true`
  and so on, while the length is carried separately in `n`. A scalar containing
  an embedded NUL would compare equal on its prefix. libyaml does not produce
  one from valid YAML; the same argument as above applies, and comparing with
  the length in hand costs nothing.

## Acceptance criteria

- [ ] An unbalanced container-end event leaves `depth` at zero and fails the document with a positioned error rather than indexing before the encoder stack. `unit: src/edge/yaml/transcode_test.c::unbalanced_end_is_refused` (drive `handle_event` directly, since libyaml will not emit the event)
- [ ] Every existing transcoder case still passes unchanged, including the depth-ceiling case. `unit: src/edge/yaml/transcode_test.c`, `src/edge/yaml/limits_test.c` (existing, must stay green)
- [ ] A scalar with an embedded NUL is encoded as the text it is, not resolved as `null` or a boolean by a prefix match. `unit: src/edge/yaml/transcode_test.c::embedded_nul_is_text`
- [ ] The secret tag is compared where it is encoded, so `scalar()` does not depend on a check made in another function. `unit: src/edge/yaml/tags_test.c` (existing, must stay green)

## Comments

This is hardening at the one place in qwe that reads untrusted bytes, so it
also sets up the fuzz target in `quality/06`: a fuzzer that drives the
transcoder will find nothing here only if the floor check exists.
