grep -q '"outcome": "success"' RUN/result.json || exit 1
[ "$(grep -c '"outcome": "success"' RUN/result.json)" = 4 ] || { echo "not all success" >&2; cat RUN/result.json >&2; exit 1; }
