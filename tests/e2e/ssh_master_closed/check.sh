# every master the run started is gone: asking each socket finds nobody
[ -f ssh.calls ] || { echo "no ssh calls recorded" >&2; exit 1; }
socks=$(sed -n 's/^-M .* -S \([^ ]*\) .*/\1/p' ssh.calls)
[ -n "$socks" ] || { echo "no master was started" >&2; exit 1; }
for s in $socks; do
	if ssh -S "$s" -O check 172.18.0.1 >/dev/null 2>&1; then
		echo "master still running on $s" >&2
		ssh -S "$s" -O exit 172.18.0.1 >/dev/null 2>&1
		exit 1
	fi
done
