# Step outputs travel on a separate channel from logs

Parsing labelled lines out of a step's stdout is the obvious way to collect outputs, and it's unsafe with secrets. If a step prints `token=…`, the tee sends the bytes to the log sink before any parser can learn the value was secret, so redaction is always one line late. Outputs therefore never go through the log stream:
- Plugin steps return them over the child's result pipe as CBOR.
- `run:` steps write them to a `$QWE_OUTPUT` file on the target, in GitHub's `$GITHUB_OUTPUT` format. The backend reads the file back after the command exits and deletes it. ssh only forwards stdin, stdout and stderr, so an extra file descriptor isn't an option.

Outputs are marked secret by declaration (a plugin's output schema, or `secret-outputs:` on a `run:` step), and their plaintext joins the run-wide redaction set before any later byte is logged.

## Consequences

- Printing `x=1` to stdout never creates an output.
- Reading outputs back from a remote target costs one more ssh round-trip per step. That's cheap over ControlMaster.
