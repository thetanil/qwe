! grep -q 'out-canary-77x' RUN/result.json || { echo "the secret is in result.json" >&2; exit 1; }
! grep -q '"outputs"' RUN/result.json || { echo "outputs in result.json" >&2; cat RUN/result.json >&2; exit 1; }
! grep -rq 'out-canary-77x' RUN || { echo "the secret is in the run directory" >&2; exit 1; }
