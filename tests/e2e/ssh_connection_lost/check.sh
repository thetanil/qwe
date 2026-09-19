# step 1 of a failed with connection-lost, and step 2 ran on a new master and succeeded
grep -q '"reason": "connection-lost"' RUN/result.json || { echo "no connection-lost" >&2; cat RUN/result.json >&2; exit 1; }
grep -q '^\[a\] recovered$' "$QWE_STDOUT" || { echo "step 2 did not run" >&2; cat "$QWE_STDOUT" >&2; exit 1; }
[ "$(grep -c '"outcome": "success"' RUN/result.json)" = 4 ] || { echo "want both jobs and a step 2 to succeed" >&2; cat RUN/result.json >&2; exit 1; }
