# b never reached its host, c depends on b, d has nothing to do with either
grep -q '"reason": "unreachable"' RUN/result.json || { echo "no unreachable reason" >&2; cat RUN/result.json >&2; exit 1; }
grep -q '"reason": "dependency-failed"' RUN/result.json || { echo "c was not skipped" >&2; exit 1; }
! grep -q engine-error RUN/result.json RUN/lifecycle.trace || { echo "engine-error for an unreachable host" >&2; exit 1; }
grep -q 'unrelated-ran' "$QWE_STDOUT" || { echo "d did not run" >&2; exit 1; }
! grep -q NEVER "$QWE_STDOUT"
