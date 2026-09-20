# only the public output is in result.json; neither secret is anywhere in the run directory
grep -q '"outputs": {"user":"alice"}' RUN/result.json || { echo "user missing from result.json" >&2; cat RUN/result.json >&2; exit 1; }
! grep -rqE 'multi-(pass|token)' RUN || { echo "a secret is in the run directory" >&2; exit 1; }
