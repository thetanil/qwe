# 02: `timeout-minutes` conversion is undefined for large values and silently zero for tiny ones

Status: ready-for-agent
Category: bug
Type: task
Blocked by: none

## What

`qwe_timeout_ms_at` (`src/kernel/jobs.c`) is the one place a `timeout-minutes:`
value becomes milliseconds:

```c
lua_getfield(L, idx, "timeout-minutes");
if (lua_isnumber(L, -1))
        ms = (long)(lua_tonumber(L, -1) * 60000.0);
lua_pop(L, 1);
return ms > 0 ? ms : 0;
```

Two problems, both from the operator's own input, and the schema constrains
only `exclusiveMinimum: 0` — there is no maximum and no floor.

**Undefined behaviour on a large value.** A `double` to `long` conversion whose
result is outside `long`'s range is undefined in C99 (6.3.1.4). `1e30` minutes
is `6e34` milliseconds, far past `LONG_MAX`. Checked on 2026-09-20:
`timeout-minutes: 1e30` did not crash and the step ran, but the value the timer
got is whatever the compiler and CPU happened to produce. This is the first
thing UBSan will stop on, and on a tool heading for safety certification,
undefined behaviour reachable from a workflow file is a finding on its own,
independent of what it happens to do today.

**A tiny value silently means "no timeout".** `0` is the sentinel for "no
limit", and truncation produces `0` for anything under one millisecond. Checked
on 2026-09-20:

```yaml
steps:
  - run: sleep 3; echo SLEPT-FULLY
    timeout-minutes: 0.000001
```

```
[j] SLEPT-FULLY
rc=0
real 0m3.020s
```

The operator asked for a timeout of 0.06 ms and got no timeout at all. It takes
a value under `1/60000` of a minute to hit, so it will not bite in practice —
but a sentinel that collides with a rounded-down real value is the kind of
thing that has to be either impossible or documented, not left to chance.

**The fix.** Clamp before converting: reject or saturate anything above a
defined maximum, and round any positive value up to at least 1 ms so that a
requested timeout is never silently dropped. A maximum in the schema
(`maximum:` alongside `exclusiveMinimum: 0`) makes the error a positioned
validation message instead of a silent clamp, which is the better outcome —
the engine then only ever sees values it can represent.

The same function serves both the job timeout and the step timeout, so one fix
covers both.

## Acceptance criteria

- [ ] `timeout-minutes: 1e30` on a job and on a step is a validation error naming the maximum, at the value's position; `qwe run` exits 2 and creates no run directory. `e2e: tests/e2e/validate_timeout_too_large/`
- [ ] No `double`-to-`long` conversion in `qwe_timeout_ms_at` can be reached with an out-of-range value: every value that reaches it has passed the schema's maximum. `unit: src/kernel/jobs_test.c::timeout_conversion_is_in_range`
- [ ] A positive `timeout-minutes` below one millisecond arms a 1 ms timer rather than no timer: a step that sleeps is killed and the step is `failed` with reason `timeout`. `e2e: tests/e2e/step_timeout_rounds_up/`
- [ ] An absent `timeout-minutes` still means no limit, and a step that outlives any plausible timer still succeeds. `e2e: tests/e2e/steps_in_order/` (existing, must stay green)
- [ ] The boundary values convert exactly: 1 minute is 60000 ms, and the schema maximum converts without overflow. `unit: src/kernel/jobs_test.c::timeout_conversion_is_in_range`
- [ ] The maximum is stated in the M1 spec's "Workflow YAML" section.

## Comments
