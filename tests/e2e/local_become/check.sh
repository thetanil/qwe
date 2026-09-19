# whatever the local sudo configuration says: with passwordless sudo the step
# runs as root, without it the step is refused with become-denied
if sudo -n true >/dev/null 2>&1; then
	grep -q '^\[j\] 0$' "$QWE_STDOUT" || { echo "want uid 0" >&2; cat "$QWE_STDOUT" >&2; exit 1; }
	! grep -q become-denied RUN/result.json || { echo "denied, but sudo -n works" >&2; exit 1; }
else
	grep -q '"reason": "become-denied"' RUN/result.json || { echo "want become-denied" >&2; cat RUN/result.json >&2; exit 1; }
fi
