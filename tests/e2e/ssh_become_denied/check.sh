# zeta has no passwordless sudo: the step is refused, at once, with become-denied
grep -q '"reason": "become-denied"' RUN/result.json || { echo "no become-denied" >&2; cat RUN/result.json >&2; exit 1; }
