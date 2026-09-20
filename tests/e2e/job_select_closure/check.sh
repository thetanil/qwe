# only a and b ran: c (needs b) and d are not in result.json
[ "$(grep -c '"outcome": "success"' RUN/result.json)" = 4 ] || { echo "want a and b, with their steps" >&2; cat RUN/result.json >&2; exit 1; }
grep -qE '^    "(c|d)": ' RUN/result.json && { echo "c or d is in result.json" >&2; exit 1; }
[ ! -e RUN/c.log ] && [ ! -e RUN/d.log ] || { echo "c or d ran" >&2; exit 1; }
