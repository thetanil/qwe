longid=$(printf 'A%.0s' $(seq 1 220))
lll=$(printf 'L%.0s' $(seq 1 300))

# The pipe in the first step's name is escaped in its table cell.
grep -qF 'a\|b' summary.md || { echo "escaped pipe not found in the id cell" >&2; exit 1; }

# The second step's 220-character name is cut with an ellipsis; the full name never appears.
! grep -qF "$longid" summary.md || { echo "the uncut long name should not appear" >&2; exit 1; }
grep -qF '…' summary.md || { echo "no ellipsis for the cut cell" >&2; exit 1; }

# The log tail's fence beats the 3-backtick run the step printed.
grep -qF '````' summary.md || { echo "no 4-backtick fence" >&2; exit 1; }

# Inside the fence, content is raw: the pipe is not escaped and the 300-char line is not cut.
grep -qF 'a ``` run and a | pipe' summary.md || { echo "pipe inside the fence should not be escaped" >&2; exit 1; }
grep -qF "$lll" summary.md || { echo "the 300-char line inside the fence should not be cut" >&2; exit 1; }
