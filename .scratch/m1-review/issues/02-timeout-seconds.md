# 02: Rename `timeout-minutes` to `timeout-seconds`, and make the conversion total

Status: ready-for-agent
Category: bug
Type: task
Blocked by: none

## What

Two changes to the same field, done together because they are the same schema
key, the same C function and the same eight fixtures.

### The unit is wrong

`timeout-minutes` came from GitHub Actions, where jobs run for tens of
minutes. qwe drives hardware: steps are seconds, and there is no realistic case
where the value is greater than one. Every fixture in the tree proves it —
every timeout in the repo is written as a fraction:

| Today | Means |
|---|---|
| `timeout-minutes: 0.005` | 0.3 s |
| `timeout-minutes: 0.02` | 1.2 s |
| `timeout-minutes: 0.03` | 1.8 s |
| `timeout-minutes: 5` | 300 s |

Nobody can read `0.005` as 300 milliseconds without reaching for a
calculator, and a misplaced decimal is a factor of ten in a timeout with no
visual cue that anything is wrong.

**`timeout-seconds` replaces it**, on both jobs and steps. Decimal parts stay
allowed, so sub-second timeouts are `0.3` and `0.05` rather than a fraction of
a minute. Seconds is the largest unit worth having: anything longer is written
as a larger number of seconds, which stays readable, and nothing needs
milliseconds as a separate unit when `0.05` says it.

This is a hard break, not a deprecation. The project is at 0.1.0, M1 is
unreleased, and carrying two spellings of one field is worse than changing it
once. But the break should be *helpful*: with `additionalProperties: false` the
old key already produces `unknown key "timeout-minutes"`, which is correct and
useless. The validator's `describe()` already special-cases keywords, so it can
recognise this one and say what to write instead, including the converted
value.

### The conversion is not total

`qwe_timeout_ms_at` (`src/kernel/jobs.c`) is the one place the value becomes
milliseconds:

```c
if (lua_isnumber(L, -1))
        ms = (long)(lua_tonumber(L, -1) * 60000.0);
return ms > 0 ? ms : 0;
```

**Undefined behaviour at the top.** A `double` to `long` conversion whose
result is outside `long`'s range is undefined (C99 6.3.1.4), and the schema
sets only `exclusiveMinimum: 0` — there is no maximum. Checked on 2026-09-20:
`timeout-minutes: 1e30` did not crash and the step ran, but the value the timer
received is whatever the compiler and CPU produced. On a tool heading for
certification, undefined behaviour reachable from a workflow file is a finding
regardless of what it happens to do, and UBSan will stop on it the day
`quality/03` lands.

**A silent zero at the bottom.** `0` is the sentinel for "no limit", and
truncation produces `0` for anything under a millisecond. Checked on
2026-09-20 with the old unit:

```yaml
- run: sleep 3; echo SLEPT-FULLY
  timeout-minutes: 0.000001
```

```
[j] SLEPT-FULLY
rc=0
real 0m3.020s
```

The operator asked for a timeout and got none. In seconds the same hole sits
below `0.001`. It takes a deliberately tiny value to reach, so it will not bite
in practice — but a sentinel colliding with a rounded-down real value has to be
made impossible rather than left to chance.

**Both are fixed by making the conversion total:** a `maximum:` in the schema
so an out-of-range value is a positioned validation error rather than a cast,
and rounding any positive value **up** to at least 1 ms so a requested timeout
is never silently dropped. The engine then only ever sees values it can
represent.

`604800` (seven days) is the proposed maximum: far beyond any soak test, far
below anything that troubles a `long`, and small enough that a typo like
`timeout-seconds: 60000000` is caught rather than armed.

## Acceptance criteria

- [ ] `timeout-seconds` works on a job and on a step, and `0.3` kills a step after ~300 ms. `e2e: tests/e2e/step_timeout_fails/`, `tests/e2e/job_timeout_cancels/` (converted from the old unit)
- [ ] `timeout-minutes` is rejected, and the error names `timeout-seconds` and the equivalent value rather than saying only "unknown key". `e2e: tests/e2e/validate_timeout_minutes_renamed/`
- [ ] `timeout-seconds: 604801` is a validation error naming the maximum, at the value's position; `qwe run` exits 2 and creates no run directory. `e2e: tests/e2e/validate_timeout_too_large/`
- [ ] No `double`-to-`long` conversion in `qwe_timeout_ms_at` can be reached with an out-of-range value: everything that reaches it has passed the schema's maximum. `unit: src/kernel/jobs_test.c::timeout_conversion_is_total`
- [ ] A positive `timeout-seconds` below one millisecond arms a 1 ms timer rather than none: a step that sleeps is killed and is `failed` with reason `timeout`. `e2e: tests/e2e/step_timeout_rounds_up/`
- [ ] The boundary values convert exactly: `1` is 1000 ms, `0.3` is 300 ms, and the maximum converts without overflow. `unit: src/kernel/jobs_test.c::timeout_conversion_is_total`
- [ ] An absent `timeout-seconds` still means no limit. `e2e: tests/e2e/steps_in_order/` (existing, must stay green)
- [ ] All eight existing fixtures are converted and still assert the same behaviour: `0.005 → 0.3`, `0.02 → 1.2`, `0.03 → 1.8`, `5 → 300`. `e2e: tests/e2e/grace_then_sigkill/`, `job_timeout_cancels/`, `parallel_limit_enforced/`, `plugin_block_timeout/`, `ssh_timeout_kills_remote/`, `step_timeout_continue/`, `step_timeout_fails/`, `validate_ok/`
- [ ] The unit, the maximum and the rounding rule are stated in design §14.

## Comments

> *This was generated by AI during triage.*

The rename came out of the `m1-review/06` grilling on 2026-09-20, where the
timeout guarantee was being pinned to a number and every worked example had to
be converted from fractional minutes to reason about at all. It is folded into
this ticket rather than opened separately because the two changes touch the
same schema key, the same function, the same fixtures and the same paragraph of
design §14 — splitting them would mean converting the fixtures twice.

`m1-review/06` sets the guarantee this field makes: a timeout fires within
100 ms of its deadline. Nothing here depends on that ticket, and it does not
depend on this one; they can land in either order.
