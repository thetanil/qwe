# A socket left by a run that was killed: its qwe pid is gone and so is its master.
rm -rf /tmp/qwe-e2e-stale
mkdir -m 700 /tmp/qwe-e2e-stale
ssh -M -N -f -S /tmp/qwe-e2e-stale/2000000000.0123456789abcdef0123456789abcdef01234567 -o BatchMode=yes 172.18.0.1 || exit 1
pkill -9 -f '[s]sh -M -N -f -S /tmp/qwe-e2e-stale/2000000000'
sleep 0.3
[ -S /tmp/qwe-e2e-stale/2000000000.0123456789abcdef0123456789abcdef01234567 ] || { echo "no stale socket" >&2; exit 1; }
