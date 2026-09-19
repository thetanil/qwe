# Pools are a static partition by convention; no reservation or locking

Several GitHub runners on one controller must not use the same device at the same time. Rather than have qwe reserve devices dynamically (which needs a lock that every run and every runner can see), each device belongs to exactly one pool, and each pool is bound to exactly one runner, named after it. CI workflows choose hardware with `runs-on: [<pool>]`. qwe provisions the runners and writes each runner's pool facts onto the controller, then steps aside. qwe doesn't run during CI. Nothing enforces the pool boundary: a runner *could* power off another pool's port with `ntgrrc`, and by agreement it doesn't.

## Consequences

- Pool membership is recorded in one place only, the device's `pool:` field, so a device can't be in two pools.
- qwe doesn't remove runners for pools that no longer exist. With three pools, runner jobs are written out by hand, one per pool.
- If enforcement is ever needed, the upgrade is a controller-side device CLI that checks the calling runner's pool.
