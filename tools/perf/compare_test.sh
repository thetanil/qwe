#!/bin/bash
# usage: compare_test.sh <compare.sh> [case...]
# Cases: no_change regression below_floor not_significant allow_versioned one_sided_keys
# sign_test_table
script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT

# row <round> <bin> <key> <us> -- appended to $t/samples.tsv
row() { printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >>"$t/samples.tsv"; }

# const_pair <key> <rounds> <a> <b> -- the same A/B pair, every round
const_pair() {
	key=$1 n=$2 a=$3 b=$4
	for r in $(seq 1 "$n"); do row "$r" A "$key" "$a"; row "$r" B "$key" "$b"; done
}

# split_pair <key> <n> <k> <a> <b_hi> <b_lo> -- A is constant; the first k rounds get
# b_hi (B > A), the rest get b_lo (B < A). Used to pin down an exact B > A count while
# keeping the same B multiset (so the median stays put) across two values of k.
split_pair() {
	key=$1 n=$2 k=$3 a=$4 b_hi=$5 b_lo=$6
	for r in $(seq 1 "$n"); do
		row "$r" A "$key" "$a"
		if [ "$r" -le "$k" ]; then row "$r" B "$key" "$b_hi"; else row "$r" B "$key" "$b_lo"; fi
	done
}

run() { "$script" "${1:-v0.2.0}" "$t/samples.tsv" "${2:-/dev/null}" "$t/out"; }

case_no_change() {
	: >"$t/samples.tsv"
	const_pair wf1/job 11 1000 1000
	run
}

case_regression() {
	: >"$t/samples.tsv"
	const_pair wf1/job 11 6667 8667 # +30%, +2ms, every round
	run && return 1
	grep -qF '`wf1/job`' "$t/out/report.md"
}

case_below_floor() {
	: >"$t/samples.tsv"
	const_pair wf1/job 11 200 300 # +50% ratio, only +100us (below the 500us step floor)
	run
}

case_not_significant() {
	: >"$t/samples.tsv"
	# B array: half at 2200 (B>A), half at 999 (B<A); median(B)=1599.5, ratio 1.6, delta
	# 599.5us all clear the gates, but only 5 of 10 rounds have B > A.
	split_pair wf1/job 10 5 1000 2200 999
	run
}

case_allow_versioned() {
	: >"$t/samples.tsv"
	const_pair wf1/job 11 6667 8667 # the same regression as case_regression
	printf 'v0.2.0 wf1/* 2.0  # known, expires when the baseline moves\n' >"$t/allow.txt"
	run v0.2.0 "$t/allow.txt" || return 1  # the matching version suppresses it
	run v0.3.0 "$t/allow.txt" && return 1  # a different baseline version does not
	return 0
}

case_one_sided_keys() {
	: >"$t/samples.tsv"
	row 1 A wfgone/job 1000
	row 2 A wfgone/job 1000
	row 1 B wfnew/job 1000
	row 2 B wfnew/job 1000
	row 1 no-baseline wfnb/job 1000
	row 2 no-baseline wfnb/job 1100
	run || return 1
	grep -qF $'wfgone/job\tgone' "$t/out/report.tsv" &&
		grep -qF $'wfnew/job\tnew' "$t/out/report.tsv" &&
		grep -qF $'wfnb/job\tno-baseline' "$t/out/report.tsv"
}

# Hand-verified critical values for the one-sided sign test at alpha = 0.01 (a single
# gated key, so no Bonferroni correction): n=11 -> 10, n=31 -> 23, n=51 -> 35.
case_sign_test_table() {
	for row in "11 10" "31 23" "51 35"; do
		set -- $row
		n=$1 crit=$2
		: >"$t/samples.tsv"
		split_pair wf1/job "$n" "$crit" 1000 2000 900
		run && { echo "n=$n k=$crit (at the critical value): expected a regression" >&2; return 1; }
		: >"$t/samples.tsv"
		split_pair wf1/job "$n" $((crit - 1)) 1000 2000 900
		run || { echo "n=$n k=$((crit - 1)) (below the critical value): expected no regression" >&2; return 1; }
	done
}

cases=${*:-no_change regression below_floor not_significant allow_versioned one_sided_keys sign_test_table}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
