want=$(ssh -o BatchMode=yes 172.18.0.1 hostname) || exit 1
[ "$(cat "$QWE_STDOUT")" = "[j] $want" ] || { echo "stdout: $(cat "$QWE_STDOUT"), want [j] $want" >&2; exit 1; }
