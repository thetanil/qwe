#!/bin/bash
# usage: expected_test.sh
#
# tools/perf/expected.tsv is well formed, and holds a wall-time expectation for every
# workflow the perf job in .github/workflows/smoke.yml times: a workflow added to the perf
# job without re-measuring (.github/workflows/perf-baseline.yml) fails here, not as a
# silent "new" key in every report.
f=tools/perf/expected.tsv
wf=.github/workflows/smoke.yml
rc=0
grep -q '^# version: v' "$f" || { echo "$f: no '# version: v...' line" >&2; rc=1; }
grep -qx $'key\tmedian_us\tp90_us\tn' "$f" || { echo "$f: no header line" >&2; rc=1; }
bad=$(awk -F'\t' '!/^#/ && $1 != "key" && !(NF == 4 && $2 ~ /^[0-9.]+$/ && $3 ~ /^[0-9.]+$/ && $4 ~ /^[0-9]+$/ && $4 >= 51)' "$f")
[ -z "$bad" ] || { echo "$f: malformed or under-sampled rows:" >&2; echo "$bad" >&2; rc=1; }
perf_wfs=$(sed -n '/tools\/perf\/run.sh/,/^ *$/p' "$wf" | grep -o 'tests/smoke/[a-z_]*\.yml' | sort -u)
[ -n "$perf_wfs" ] || { echo "$wf: found no workflow passed to tools/perf/run.sh" >&2; rc=1; }
for w in $perf_wfs; do
	k=$(basename "$w" .yml)/wall
	grep -q "^$k	" "$f" || { echo "$f: no expectation for $k (timed by the perf job)" >&2; rc=1; }
done
[ $rc -eq 0 ] && echo "PASS: $f covers $(echo "$perf_wfs" | wc -w) perf workflow(s)"
exit $rc
