# `ntgrrc login` is the one allowed exception to "secrets never go in argv"

qwe never puts a secret on a command line (qwe-ssh-sec II.8). Env and stdin carry secrets through the stdin preamble. But `ntgrrc login` accepts the switch password only as `--password`, or through an interactive prompt that uses `term.ReadPassword`, which requires a TTY. GitHub runners on the controller also need to be able to log in again on their own when the token expires. So qwe sends the password to the controller over stdin into a mode-0600 file, and installs a controller-side login wrapper that reads the file and calls `ntgrrc login --password …`. The password is in argv only on the controller, only for the duration of the login, and never in any command line qwe itself builds.

## Considered options

- **A pseudo-terminal (`ssh -tt`) and typing the password into the prompt.** Rejected. It's sensitive to timing, clashes with capturing TTY echo, and doesn't help runners.
- **Posting the switch's login form directly and writing ntgrrc's token file.** Rejected. It depends on ntgrrc's internal token format and the switch's login protocol.

## Consequences

- If ntgrrc ever gains a stdin or environment-variable password option, remove this exception.
