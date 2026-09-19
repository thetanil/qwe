# a starts first (id order) and alone: it waits for b's marker, which never comes.
grep -A2 '"a": {' RUN/result.json | grep -q '"outcome": "failed"' || { cat RUN/result.json >&2; exit 1; }
sed -n '/"a": {/,/"b": {/p' RUN/result.json | grep -q '"reason": "timeout"' || exit 1
