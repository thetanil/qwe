# a (needed by b), b and d ran; c did not
for j in a b d; do grep -qE "^    \"$j\": " RUN/result.json || { echo "$j missing" >&2; exit 1; }; done
grep -qE '^    "c": ' RUN/result.json && { echo "c is in result.json" >&2; exit 1; }
[ "$(grep -c '"outcome": "success"' RUN/result.json)" = 6 ] || { echo "not all success" >&2; exit 1; }
