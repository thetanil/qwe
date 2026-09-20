# 0.4 ms is rounded up to 1 ms, not down to "no limit": the sleep is killed.
grep -q '"reason": "timeout"' RUN/result.json
