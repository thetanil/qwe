# qwe was SIGKILLed mid-step and closed nothing: the master is still there now...
pgrep -f '[s]sh -M -N -f -S xdgkill/qwe/' >/dev/null || { echo "no master was left to expire" >&2; exit 1; }
# ...and goes by itself once the ttl (2 s here) has passed.
i=0
while pgrep -f '[s]sh -M -N -f -S xdgkill/qwe/' >/dev/null; do
	i=$((i + 1))
	[ $i -le 100 ] || { echo "master still running after 10 s" >&2; pkill -f '[s]sh -M -N -f -S xdgkill/qwe/'; exit 1; }
	sleep 0.1
done
