For the next session:
- Read first: CONTEXT.md, docs/adr/ (0001–0009), .scratch/m1-engine/spec.md, then the tickets in .scratch/m1-engine/issues/.
- Start with ticket 01 (Bazel skeleton). It has no dependencies. Ticket 03, the first end-to-end run, needs only 01 now that 02 is resolved.
- Still open:
  - error positions inside plugin schema.json files,
  - the secret tag number and the type: string question (both in ticket 15),
  - whether dkjson works under strict globals (not verified).
