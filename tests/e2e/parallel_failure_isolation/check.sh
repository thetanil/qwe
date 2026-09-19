job() { sed -n "/\"$1\": {/,/\"steps\"/p" RUN/result.json; }
job a | grep -q '"outcome": "failed"' || { echo "a not failed" >&2; exit 1; }
job b | grep -q '"outcome": "success"' || { echo "b not success" >&2; exit 1; }
job c | grep -q '"outcome": "skipped"' || { echo "c not skipped" >&2; exit 1; }
job c | grep -q '"reason": "dependency-failed"' || { echo "c wrong reason" >&2; exit 1; }
grep -q "b finished" RUN/b.log || { echo "b was cut short" >&2; exit 1; }
! grep -rq NEVER RUN "$QWE_STDOUT" || { echo "a skipped job ran" >&2; exit 1; }
