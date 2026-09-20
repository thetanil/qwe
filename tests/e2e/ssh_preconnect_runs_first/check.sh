# the master is started before any step's ssh, and once for the three jobs
masters=$(grep -c '^-M ' ssh.calls)
[ "$masters" = 1 ] || { echo "want 1 master, got $masters" >&2; cat ssh.calls >&2; exit 1; }
first_step=$(grep -n 'ControlMaster=no' ssh.calls | head -1 | cut -d: -f1)
last_master=$(grep -n '^-M ' ssh.calls | tail -1 | cut -d: -f1)
[ -n "$first_step" ] && [ "$last_master" -lt "$first_step" ] || { echo "a master started after a step began" >&2; cat ssh.calls >&2; exit 1; }
