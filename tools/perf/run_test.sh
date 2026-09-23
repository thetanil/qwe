#!/bin/bash
# usage: run_test.sh <run.sh> [case...]
# Cases: warm_up_discarded abba_order no_baseline_detected baseline_none
#
# A stub qwe (run <file>: writes a canned result.json under .qwe/runs/<id>/, sleeps a
# fixed amount, and fails a workflow named in QWE_FAIL_ON) stands in for the real binary,
# so these cases check run.sh's own bookkeeping (round counting, ABBA ordering, the
# warm-up discard, no-baseline detection) without needing a built qwe or real timing.
script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT

mkdir -p "$t/wf"
printf 'jobs: {}\n' >"$t/wf/smoke_a.yml"
printf 'jobs: {}\n' >"$t/wf/smoke_b.yml"

stub() { # stub <path> [fail-on-workflow-basename]
	failon=${2:-}
	cat >"$1" <<SCRIPT
#!/bin/bash
set -eu
wf=\$2
if [ -n "$failon" ] && [ "\$wf" = "$failon" ]; then
	echo "stub: simulated failure for \$wf" >&2
	exit 1
fi
rid=\$(date +%s%N)
mkdir -p ".qwe/runs/\$rid"
echo '{"jobs":{"j":{"duration_ms":8,"steps":[{"duration_ms":2}]}}}' >".qwe/runs/\$rid/result.json"
SCRIPT
	chmod +x "$1"
}

samples() { cat "$t/out/samples.tsv"; }

case_warm_up_discarded() {
	stub "$t/cand"; stub "$t/base"
	"$script" "$t/out" 1 "$t/cand" "$t/base" "$t/wf/smoke_a.yml" >/dev/null 2>&1 || return 1
	! grep -q '^0	' "$t/out/samples.tsv"
}

case_abba_order() {
	stub "$t/cand"; stub "$t/base"
	"$script" "$t/out" 2 "$t/cand" "$t/base" "$t/wf/smoke_a.yml" >/dev/null 2>&1 || return 1
	# round 1: A before B; round 2: B before A (ABBA, alternating by round)
	seq1=$(awk -F'\t' '$1==1{print $2}' "$t/out/samples.tsv" | head -1)
	seq2=$(awk -F'\t' '$1==2{print $2}' "$t/out/samples.tsv" | head -1)
	[ "$seq1" = A ] && [ "$seq2" = B ]
}

case_no_baseline_detected() {
	stub "$t/cand"; stub "$t/base" smoke_b.yml
	"$script" "$t/out" 2 "$t/cand" "$t/base" "$t/wf/smoke_a.yml" "$t/wf/smoke_b.yml" >/dev/null 2>&1 || return 1
	# smoke_a keeps a real A/B pair every round; smoke_b never gets an A row again after
	# the warm-up failure, and is marked no-baseline throughout the timed rounds instead.
	[ "$(awk -F'\t' '$3=="smoke_a/wall" && $2=="A"' "$t/out/samples.tsv" | wc -l)" -eq 2 ] &&
		[ "$(awk -F'\t' '$3=="smoke_b/wall" && $2=="A"' "$t/out/samples.tsv" | wc -l)" -eq 0 ] &&
		[ "$(awk -F'\t' '$3=="smoke_b/wall" && $2=="no-baseline"' "$t/out/samples.tsv" | wc -l)" -eq 2 ]
}

case_baseline_none() {
	stub "$t/cand"
	"$script" "$t/out" 1 "$t/cand" "" "$t/wf/smoke_a.yml" >/dev/null 2>&1 || return 1
	# no A rows at all: tools/perf/compare.sh would classify every key "new" (report-only).
	[ "$(awk -F'\t' '$2=="A"' "$t/out/samples.tsv" | wc -l)" -eq 0 ] &&
		[ "$(awk -F'\t' '$2=="B" && $3=="smoke_a/wall"' "$t/out/samples.tsv" | wc -l)" -eq 1 ]
}

cases=${*:-warm_up_discarded abba_order no_baseline_detected baseline_none}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
