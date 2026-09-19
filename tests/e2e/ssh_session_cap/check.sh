[ "$(grep -c '"outcome": "success"' RUN/result.json)" = 4 ] || { echo "a job failed" >&2; cat RUN/result.json >&2; exit 1; }
