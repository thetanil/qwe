#!/bin/bash
# usage: expect_test.sh <expect.sh> [case...]
# Cases: pools_named_bins default_bin_is_b median_and_p90 notes_and_sources missing_file no_match
script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT
mkdir -p "$t/r1" "$t/r2"

row() { printf '%s\t%s\t%s\t%s\n' "$2" "$3" "$4" "$5" >>"$1"; }

# value <file> <key> -> median_us p90_us n
value() { awk -F'\t' -v k="$2" '$1==k {print $2, $3, $4}' "$1"; }

case_pools_named_bins() {
	: >"$t/r1/samples.tsv"; : >"$t/r2/samples.tsv"
	row "$t/r1/samples.tsv" 1 A wf/wall 100
	row "$t/r1/samples.tsv" 1 B wf/wall 999 # the other binary: must not be pooled
	row "$t/r2/samples.tsv" 1 no-baseline wf/wall 300
	row "$t/r2/samples.tsv" 2 B wf/wall 200
	"$script" v1 "$t/out.tsv" "$t/r1/samples.tsv:A" "$t/r2/samples.tsv:B,no-baseline" || return 1
	[ "$(value "$t/out.tsv" wf/wall)" = "200 300 3" ]
}

case_default_bin_is_b() {
	: >"$t/r1/samples.tsv"
	row "$t/r1/samples.tsv" 1 A wf/wall 100
	row "$t/r1/samples.tsv" 1 B wf/wall 500
	"$script" v1 "$t/out.tsv" "$t/r1/samples.tsv" || return 1
	[ "$(value "$t/out.tsv" wf/wall)" = "500 500 1" ]
}

case_median_and_p90() {
	: >"$t/r1/samples.tsv"
	for v in 10 1 9 2 8 3 7 4 6 5; do row "$t/r1/samples.tsv" 1 B wf/job "$v"; done
	"$script" v1 "$t/out.tsv" "$t/r1/samples.tsv" || return 1
	# even n: the mean of the middle two (5, 6); p90 of 10: the 9th smallest
	[ "$(value "$t/out.tsv" wf/job)" = "5.5 9 10" ]
}

case_notes_and_sources() {
	: >"$t/r1/samples.tsv"
	row "$t/r1/samples.tsv" 1 B wf/wall 1
	"$script" --note "runner: test cpu" v9.9.9 "$t/out.tsv" "$t/r1/samples.tsv" || return 1
	head -1 "$t/out.tsv" | grep -qx '# version: v9.9.9' &&
		grep -qx '# runner: test cpu' "$t/out.tsv" &&
		grep -qx '# source: r1/samples.tsv bins=B rows=1' "$t/out.tsv" &&
		grep -qx $'key\tmedian_us\tp90_us\tn' "$t/out.tsv"
}

case_missing_file() {
	"$script" v1 "$t/out.tsv" "$t/nope.tsv" 2>/dev/null
	[ $? -eq 3 ]
}

case_no_match() {
	: >"$t/r1/samples.tsv"
	row "$t/r1/samples.tsv" 1 A wf/wall 1
	"$script" v1 "$t/out.tsv" "$t/r1/samples.tsv" 2>/dev/null
	[ $? -eq 1 ]
}

cases=${*:-pools_named_bins default_bin_is_b median_and_p90 notes_and_sources missing_file no_match}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
