# 04: The ControlPath directory is used without checking who owns it

Status: resolved
Category: bug
Type: task
Blocked by: none

## What

`backend.ssh` puts its control sockets in `/tmp/qwe-<uid>/`, and creates that
directory like this:

```lua
exec.run({ "mkdir", "-p", "-m", "700", m.dir })
```

The result is not checked, and `-m` only applies to a directory `mkdir`
actually creates. Checked on 2026-09-20:

```
$ mkdir -m 777 mkt && stat -c %a mkt
777
$ mkdir -p -m 700 mkt && stat -c %a mkt
777
```

So if the directory already exists, qwe accepts it whatever its mode and
whoever owns it. `/tmp` is world-writable and `/tmp/qwe-1000` does not exist
until qwe first runs, so another local user can create it first, keep it
writable, and wait. qwe then puts its ControlMaster socket inside a directory
that user controls.

What that buys the attacker is not nothing: they can remove the socket and bind
their own at the same path between qwe's `-O check` and the next step's
`ssh -S`. Every command qwe sends to that target then goes to a socket they
own — they see the command text, and they choose the output and the exit status
qwe reads back. Since the step's env arrives on stdin through the preamble,
that includes any `${{ secrets.X }}` mapped into the step's environment.

It needs a local account on the operator host and a race, so it is not the
first thing an attacker reaches for. It is also the kind of finding that is
cheap to close now and awkward to explain later.

**The fix.** Prefer `$XDG_RUNTIME_DIR` when it is set: it is already per-user
and mode 0700, created by the system, and it is where a runtime socket belongs.
Fall back to `/tmp/qwe-<uid>` only when it is unset, and in that case verify
rather than assume — after `mkdir`, `stat` the directory and refuse to use it
unless it is a real directory, owned by this uid, with no group or other
permission bits. That is the same check `qwe_key_load` already makes on the key
file, and the same check `ssh` makes on a private key, so the rule is
consistent across the tool.

Refusing should fail the step with a clear message naming the directory and
what is wrong with it, not fall back to a less safe path.

## Acceptance criteria

- [x] With `XDG_RUNTIME_DIR` set to a writable 0700 directory, the control socket is created under it and no directory is created in `/tmp`. `e2e: tests/e2e/ssh_socket_dir_xdg/`
- [x] With `XDG_RUNTIME_DIR` unset, `/tmp/qwe-<uid>` is created 0700 and used. `e2e: tests/e2e/ssh_hostname/` (existing, must stay green)
- [x] A pre-existing socket directory with mode 0777 is refused: the step fails, the message names the directory and its mode, and no socket is created in it. `e2e: tests/e2e/ssh_socket_dir_refused/`
- [ ] **Deliberately unverified — see Comments.** A socket directory that is not owned by the current user is refused the same way. `manual: as a second local user, create /tmp/qwe-<uid> mode 0700, then run an ssh e2e case as the first user and confirm it refuses by owner rather than by mode` (an automated case cannot make a directory owned by another uid)
- [x] The directory check and the key-file check report their problems the same way, so the rule reads as one rule. `unit: src/secrets/keyfile_test.c::mode_check_matches_socket_dir_check`
- [x] qwe-ssh-sec I.2 records `$XDG_RUNTIME_DIR` as the preferred ControlPath location and the ownership check as the fallback's condition.

## Comments

The ssh e2e cases carry `needs-ssh` and skip without a reachable target, so the
two new cases follow that convention.

Resolved 2026-09-20. `qwe_private_check` (src/secrets/keyfile.c) is now the one owner-and-mode rule; `qwe_key_load` and the new `qwe.fs.private_dir` both call it, so the wording matches (`keyfile_test.c`). `backend.ssh.ensure` verifies the directory on every call, before `ssh -O check`, and returns the message, which the engine reports as a step start failure. `$XDG_RUNTIME_DIR/qwe` is used when set and short enough for a socket path, else `/tmp/qwe-<uid>`. The key-file check gained the owner test as well. The two new e2e cases set `XDG_RUNTIME_DIR=xdg` (relative, as `key_perms_refused` does with HOME); the refused case pre-creates `xdg/qwe` 0777. The manual second-user owner check was not run: there is no second local user here. `ssh_connection_lost` matched the socket path literally as `/tmp/qwe-<uid>/`; it now matches any directory.

Follow-up 2026-09-20: no e2e case depends on `$XDG_RUNTIME_DIR` any more (its owner and mode vary by machine). `run_case.sh` unsets it, `QWE_TEST_SOCKET_DIR` pins the directory (fixed `/tmp/qwe-e2e-*` names), and `ssh_socket_dir_xdg` is deleted; XDG preference is covered by the `socket_dir_prefers_the_runtime_dir` Lua unit case only, so the first criterion is no longer an e2e.

Closed 2026-09-20 with the owner criterion **deliberately unverified**, not
pending. No automated case can reach it — a directory owned by another uid needs
a second local account, and this machine has one user — so the box stays
unticked, because ticking it would claim a check that was never run, and the
criterion is marked in place so the requirement-to-test map reads honestly.
Nothing is waiting on it; it is not work anybody should pick up.

What is actually covered, so the size of the gap is on the record.
`qwe_private_check` (`src/secrets/keyfile.c:36`) is one function over one
`stat`: the owner arm first, the mode arm second, both returning `-1` with the
same message shape. The **owner arm is unit-tested** —
`keyfile_test.c::mode_check_matches_socket_dir_check` fabricates a `struct stat`
with `st_uid = 1` and asserts that the key file and the socket directory both
report `is owned by uid 1` — so the comparison's sense is proven, not assumed.
`ssh_socket_dir_refused` proves end to end that `backend.ssh` stats the real
directory before use and turns a refusal into a failed step naming it, through
the mode arm. The manual criterion is the join of those two: a genuinely
foreign-owned directory travelling the whole path. Since the caller cannot tell
the two arms apart, what it would add over what is tested is small, and it is
recorded here rather than left as an open box. If a machine running this suite
ever has a second account, the steps above still close it.
