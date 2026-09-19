# No child is left behind: nothing named sleep with this run's marker argument is alive.
for p in /proc/[0-9]*; do
	[ "$(cat "$p/comm" 2>/dev/null)" = sleep ] || continue
	if tr '\0' ' ' < "$p/cmdline" 2>/dev/null | grep -q "$QWE_E2E_MARK"; then
		echo "orphan left behind: $p" >&2
		exit 1
	fi
done
! grep -rq NEVER RUN "$QWE_STDOUT" || { echo "a skipped job ran" >&2; exit 1; }
[ "$(grep -c '"outcome": "cancelled"' RUN/result.json)" = 6 ] || { echo "want 3 jobs and 3 steps cancelled" >&2; cat RUN/result.json >&2; exit 1; }
[ "$(grep -c '"reason": "cancel-requested"' RUN/result.json)" = 6 ] || { echo "wrong reasons" >&2; exit 1; }
