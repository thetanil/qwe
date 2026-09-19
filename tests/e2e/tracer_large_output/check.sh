# fold adds a newline after every full 1023 bytes: 10485760 = 10250*1023 + 10,
# and none after the 10-byte tail.
# Every byte must reach the log, and none may be dropped.
want=$((10485760 + 10250))
got=$(wc -c < RUN/j.log)
[ "$got" -eq "$want" ] || { echo "j.log has $got bytes, want $want" >&2; exit 1; }
grep -q '"dropped_bytes": 0' RUN/result.json || { echo "bytes were dropped" >&2; exit 1; }
