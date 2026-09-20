# from the timeout to the step's end (the TERM's teardown) the parent is not held up by the kill
awk '$2=="j" && $5=="step-timeout" {s=$1} $2=="j" && $5=="group-empty" {e=$1} END {d=e-s; exit !(d >= 0 && d <= 0.1)}' lifecycle.raw || {
	echo "the step took too long to end after its timeout:" >&2
	cat lifecycle.raw >&2
	exit 1
}
i=0
while [ $i -lt 40 ]; do
	ssh -o BatchMode=yes 172.18.0.1 "pgrep -f '[s]leep 27183'" >/dev/null || exit 0
	sleep 0.1
	i=$((i + 1))
done
ssh -o BatchMode=yes 172.18.0.1 "pkill -f '[s]leep 27183'" >/dev/null
echo "a remote sleep outlived the step" >&2
exit 1
