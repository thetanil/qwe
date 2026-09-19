# [s]leep: the pattern does not match the command line that holds it
i=0
while [ $i -lt 40 ]; do
	ssh -o BatchMode=yes 172.18.0.1 "pgrep -f '[s]leep 27182'" >/dev/null || exit 0
	sleep 0.1
	i=$((i + 1))
done
ssh -o BatchMode=yes 172.18.0.1 "pkill -f '[s]leep 27182'" >/dev/null
echo "a remote sleep outlived the step" >&2
exit 1
