#!/bin/bash
# usage: run.sh <outdir> <rounds> <candidate-bin> <baseline-bin> <workflow-file>...
#
# The A/B sample collector behind the perf job (15). <baseline-bin> empty ("") means there
# is no baseline at all (input baseline: none): every workflow only ever runs the
# candidate, so every key comes out of tools/perf/compare.sh classified "new" -- report-only,
# by construction, with no special case needed here or there.
#
# With a real baseline: one warm-up round runs first (both sides, every workflow) and is
# discarded, only to decide, per workflow, whether the baseline can run it at all. A
# workflow whose baseline fails the warm-up is marked no-baseline for the rest of this run:
# the candidate keeps running and being timed every round, the baseline is never retried.
#
# Then <rounds> timed rounds run in ABBA order: odd rounds run A then B, even rounds run B
# then A, so a systematic drift across the run (a warming cache, thermal throttling) lands
# on both sides evenly instead of biasing whichever binary always went first.
#
# Writes <outdir>/samples.tsv: `round\tbin\tkey\tus`, bin one of A, B or no-baseline, key
# one of <workflow>/wall, <workflow>/<job> or <workflow>/<job>/<step-index> (the step's
# position in its job's steps array). <workflow> is the workflow file's basename, without
# .yml. A qwe failure on the candidate side is fatal (that binary must work); a baseline
# failure outside the warm-up round is logged and the workflow is marked no-baseline from
# then on, same as a warm-up failure.
set -u

outdir=$1 rounds=$2 cand_bin=$3 base_bin=$4
shift 4
workflows=("$@")

mkdir -p "$outdir"
samples="$outdir/samples.tsv"
: >"$samples"

cand_bin=$(readlink -f "$cand_bin")
[ -n "$base_bin" ] && base_bin=$(readlink -f "$base_bin")

nb_marker() { echo "$outdir/no-baseline.$(basename "$1" .yml)"; }

# time_and_record <round> <bin> <bin-path> <workflow>  ->  0 on success, 1 if qwe failed
time_and_record() {
	round=$1 bin=$2 binpath=$3 wf=$4
	wfdir=$(dirname "$wf") wfname=$(basename "$wf") wfkey=$(basename "$wf" .yml)
	before=$(date +%s%N)
	(cd "$wfdir" && "$binpath" run "$wfname") >"$outdir/last-run.log" 2>&1
	rc=$?
	after=$(date +%s%N)
	if [ "$rc" -ne 0 ]; then
		echo "perf: $binpath run $wf exited $rc" >&2
		cat "$outdir/last-run.log" >&2
		return 1
	fi
	run_id=$(ls -1t "$wfdir/.qwe/runs" 2>/dev/null | head -1)
	result="$wfdir/.qwe/runs/$run_id/result.json"
	[ -f "$result" ] || { echo "perf: no result.json for $wf under $wfdir/.qwe/runs/$run_id" >&2; return 1; }

	wall_us=$(((after - before) / 1000))
	printf '%s\t%s\t%s/wall\t%s\n' "$round" "$bin" "$wfkey" "$wall_us" >>"$samples"

	{
		jq -r '.jobs | to_entries[] | select(.value.duration_ms != null) | "\(.key)\t\(.value.duration_ms)"' "$result"
		jq -r '.jobs | to_entries[] | .key as $j | .value.steps | to_entries[] | select(.value.duration_ms != null) | "\($j)/\(.key)\t\(.value.duration_ms)"' "$result"
	} | while IFS="$(printf '\t')" read -r k ms; do
		printf '%s\t%s\t%s/%s\t%s\n' "$round" "$bin" "$wfkey" "$k" "$((ms * 1000))" >>"$samples"
	done
	return 0
}

if [ -n "$base_bin" ]; then
	echo "perf: warm-up round (discarded)"
	for wf in "${workflows[@]}"; do
		nb=$(nb_marker "$wf")
		if ! time_and_record 0 A "$base_bin" "$wf"; then
			touch "$nb"
			echo "perf: baseline cannot run $wf; marked no-baseline for this run" >&2
		fi
		time_and_record 0 B "$cand_bin" "$wf" || exit 1
	done
	grep -v '^0	' "$samples" >"$samples.tmp" || :
	mv "$samples.tmp" "$samples"
else
	echo "perf: no baseline (report-only): every key will be new"
fi

r=1
while [ "$r" -le "$rounds" ]; do
	for wf in "${workflows[@]}"; do
		nb=$(nb_marker "$wf")
		a_side() {
			[ -n "$base_bin" ] && [ ! -e "$nb" ] || return 0
			time_and_record "$r" A "$base_bin" "$wf" || touch "$nb"
		}
		b_side() {
			if [ -n "$base_bin" ] && [ -e "$nb" ]; then bin=no-baseline; else bin=B; fi
			time_and_record "$r" "$bin" "$cand_bin" "$wf" || exit 1
		}
		if [ $((r % 2)) -eq 1 ]; then a_side; b_side; else b_side; a_side; fi
	done
	r=$((r + 1))
done

echo "perf: $((${#workflows[@]})) workflow(s), $rounds round(s) recorded to $samples"
