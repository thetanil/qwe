#!/bin/bash
# usage: run_test.sh <run.sh> [case...]
# Cases: warm_up_discarded candidate_only rounds_counted candidate_failure_fatal
#
# A stub qwe (run <file>: writes a canned result.json under .qwe/runs/<id>/, and fails a
# workflow named in its second argument) stands in for the real binary, so these cases check
# run.sh's own bookkeeping without needing a built qwe or real timing.
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

case_warm_up_discarded() {
	stub "$t/cand"
	"$script" "$t/out" 1 "$t/cand" "$t/wf/smoke_a.yml" >/dev/null 2>&1 || return 1
	! grep -q '^0	' "$t/out/samples.tsv"
}

case_candidate_only() {
	stub "$t/cand"
	"$script" "$t/out" 2 "$t/cand" "$t/wf/smoke_a.yml" "$t/wf/smoke_b.yml" >/dev/null 2>&1 || return 1
	[ "$(awk -F'\t' '$2!="B"' "$t/out/samples.tsv" | wc -l)" -eq 0 ] &&
		grep -qF $'\tsmoke_a/j/0\t2000' "$t/out/samples.tsv" &&
		grep -qF $'\tsmoke_b/j\t8000' "$t/out/samples.tsv"
}

case_rounds_counted() {
	stub "$t/cand"
	"$script" "$t/out" 3 "$t/cand" "$t/wf/smoke_a.yml" >/dev/null 2>&1 || return 1
	[ "$(awk -F'\t' '$3=="smoke_a/wall"' "$t/out/samples.tsv" | wc -l)" -eq 3 ]
}

case_candidate_failure_fatal() {
	stub "$t/cand" smoke_b.yml
	! "$script" "$t/out" 1 "$t/cand" "$t/wf/smoke_a.yml" "$t/wf/smoke_b.yml" >/dev/null 2>&1
}

cases=${*:-warm_up_discarded candidate_only rounds_counted candidate_failure_fatal}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
