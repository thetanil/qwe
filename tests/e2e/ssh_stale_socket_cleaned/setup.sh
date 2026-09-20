# A socket left by a run that was killed: its qwe pid is gone and so is its master.
mkdir -m 700 xdgstale
mkdir -m 700 xdgstale/qwe
ssh -M -N -f -S xdgstale/qwe/2000000000.0123456789abcdef0123456789abcdef01234567 -o BatchMode=yes 172.18.0.1 || exit 1
pkill -9 -f '[s]sh -M -N -f -S xdgstale/qwe/2000000000'
sleep 0.3
[ -S xdgstale/qwe/2000000000.0123456789abcdef0123456789abcdef01234567 ] || { echo "no stale socket" >&2; exit 1; }
