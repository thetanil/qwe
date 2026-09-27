#!/bin/bash
# usage: run.sh <outdir> <rounds> <candidate-bin> <workflow-file>...
#
# The sample collector behind the perf job. It times only the candidate: the comparison is
# against stored expected values (tools/perf/expected.tsv), not a second binary
# (docs/adr/0016-perf-gate-compares-with-stored-expectations.md).
#
# One warm-up round runs first, every workflow, and is discarded (cold caches, first page
# faults). Then <rounds> timed rounds run every workflow in order.
#
# Writes <outdir>/samples.tsv: `round\tB\tkey\tus`. The bin column is always B (the
# candidate); it is kept so tools/perf/expect.sh can pool these files and older A/B ones the
# same way. key is one of <workflow>/wall, <workflow>/<job> or <workflow>/<job>/<step-index>
# (the step's position in its job's steps array). <workflow> is the workflow file's basename,
# without .yml. A qwe failure is fatal: the candidate must work.
set -u

[ $# -ge 4 ] || {
	echo "usage: run.sh <outdir> <rounds> <candidate-bin> <workflow-file>..." >&2
	exit 3
}
outdir=$1 rounds=$2 cand_bin=$3
shift 3
workflows=("$@")

mkdir -p "$outdir"
samples="$outdir/samples.tsv"
: >"$samples"

cand_bin=$(readlink -f "$cand_bin")

# time_and_record <round> <workflow>  ->  0 on success, 1 if qwe failed
time_and_record() {
	round=$1 wf=$2
	wfdir=$(dirname "$wf") wfname=$(basename "$wf") wfkey=$(basename "$wf" .yml)
	before=$(date +%s%N)
	(cd "$wfdir" && "$cand_bin" run "$wfname") >"$outdir/last-run.log" 2>&1
	rc=$?
	after=$(date +%s%N)
	if [ "$rc" -ne 0 ]; then
		echo "perf: $cand_bin run $wf exited $rc" >&2
		cat "$outdir/last-run.log" >&2
		return 1
	fi
	run_id=$(ls -1t "$wfdir/.qwe/runs" 2>/dev/null | head -1)
	result="$wfdir/.qwe/runs/$run_id/result.json"
	[ -f "$result" ] || { echo "perf: no result.json for $wf under $wfdir/.qwe/runs/$run_id" >&2; return 1; }

	wall_us=$(((after - before) / 1000))
	printf '%s\tB\t%s/wall\t%s\n' "$round" "$wfkey" "$wall_us" >>"$samples"

	{
		jq -r '.jobs | to_entries[] | select(.value.duration_ms != null) | "\(.key)\t\(.value.duration_ms)"' "$result"
		jq -r '.jobs | to_entries[] | .key as $j | .value.steps | to_entries[] | select(.value.duration_ms != null) | "\($j)/\(.key)\t\(.value.duration_ms)"' "$result"
	} | while IFS="$(printf '\t')" read -r k ms; do
		printf '%s\tB\t%s/%s\t%s\n' "$round" "$wfkey" "$k" "$((ms * 1000))" >>"$samples"
	done
	return 0
}

echo "perf: warm-up round (discarded)"
for wf in "${workflows[@]}"; do
	time_and_record 0 "$wf" || exit 1
done
: >"$samples"

r=1
while [ "$r" -le "$rounds" ]; do
	for wf in "${workflows[@]}"; do
		time_and_record "$r" "$wf" || exit 1
	done
	r=$((r + 1))
done

echo "perf: ${#workflows[@]} workflow(s), $rounds round(s) recorded to $samples"
