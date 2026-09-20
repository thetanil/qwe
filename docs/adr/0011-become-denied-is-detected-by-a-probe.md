# `become-denied` is detected by a probe, not by an exit status

A step with `become:` runs its command under `sudo -n`. When sudo refuses — no
passwordless rule for that user, or a password would be required — it prints a
message and exits **1**. A command that ran fine under sudo and then failed on
its own also exits 1. The exit status cannot tell the two apart, and neither
can stderr without parsing sudo's message, which is localised and version-
dependent.

So before the step's real command runs, qwe runs `true` through the same
backend with the same `become:` prefix. If that fails, sudo will not let this
step through, and the step is `failed` with reason `become-denied` before any
of its work starts. If it succeeds, the real command runs and its exit status
means what it says.

The alternatives were worse. Trusting exit 1 would report `become-denied` for
every ordinary command failure in a `become:` step. Parsing sudo's stderr binds
qwe to the wording of a message that is not a stable interface. Letting the
step run and inferring afterwards gives the wrong answer in the case that
matters most: a step that is refused has changed nothing, and saying so is the
whole point of a distinct reason.

## Consequences

- A `become:` step costs one extra round trip — an extra local fork, or an
  extra ssh session on a remote target — before its command. Steps without
  `become:` are unaffected: the probe is skipped when the prefix is empty.
- The probe runs in the step's forked child, through the job's own backend, so
  it does not block the parent's event loop and it exercises exactly the path
  the real command will take, including the ssh session and the preamble.
- A refusal is reported on the result pipe as
  `{status = failed, reason = become-denied, changed = false}`, so `changed` is
  honestly false — nothing ran.
- On a remote target, ssh's own failure status (255) is excluded from the
  probe's verdict, so a connection problem during the probe is not misreported
  as a sudo refusal; it fails later as `connection-lost`.
- The window between the probe and the command is not atomic. If sudo's policy
  changes in between, the command fails on its own status instead of as
  `become-denied`. That race is accepted: the alternative is no detection at
  all.
