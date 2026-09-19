n=$(grep -c -- '^-M ' ssh.calls)
[ "$n" = 1 ] || { echo "masters started: $n" >&2; cat ssh.calls >&2; exit 1; }
