#!/bin/bash
# usage: expected_test.sh
#
# tools/perf/expected.tsv is well formed, and holds a wall-time expectation for every
# workflow in tools/perf/workflows.txt (the list the perf job and
# .github/workflows/perf-baseline.yml both time): a workflow added to the list without
# re-measuring fails here, not as a silent "new" key in every report.
f=tools/perf/expected.tsv
list=tools/perf/workflows.txt
rc=0
grep -q '^# version: v' "$f" || { echo "$f: no '# version: v...' line" >&2; rc=1; }
grep -qx $'key\tmedian_us\tp90_us\tn' "$f" || { echo "$f: no header line" >&2; rc=1; }
bad=$(awk -F'\t' '!/^#/ && $1 != "key" && !(NF == 4 && $2 ~ /^[0-9.]+$/ && $3 ~ /^[0-9.]+$/ && $4 ~ /^[0-9]+$/ && $4 >= 51)' "$f")
[ -z "$bad" ] || { echo "$f: malformed or under-sampled rows:" >&2; echo "$bad" >&2; rc=1; }
[ -s "$list" ] || { echo "$list: empty" >&2; rc=1; }
n=0
while IFS= read -r w; do
	[ -n "$w" ] || continue
	n=$((n + 1))
	[ -f "$w" ] || { echo "$list: $w does not exist" >&2; rc=1; }
	k=$(basename "$w" .yml)/wall
	grep -q "^$k	" "$f" || { echo "$f: no expectation for $k (in $list)" >&2; rc=1; }
done <"$list"
[ $rc -eq 0 ] && echo "PASS: $f covers $n perf workflow(s)"
exit $rc
