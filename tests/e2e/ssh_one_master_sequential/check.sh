n=$(grep -c -- '^-M ' ssh.calls)
[ "$n" = 1 ] || { echo "masters started: $n" >&2; cat ssh.calls >&2; exit 1; }
[ "$(grep -c '^\[j\] step' "$QWE_STDOUT")" = 10 ] || { echo "not all ten steps ran" >&2; exit 1; }
