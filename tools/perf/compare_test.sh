#!/bin/bash
# usage: compare_test.sh <compare.sh> [case...]
# Cases: no_change regression below_floor not_significant allow_versioned one_sided_keys
# sign_test_table no_version
script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT

# expected <version> -- starts $t/expected.tsv; exp <key> <median_us> adds a key
expected() { printf '# version: %s\nkey\tmedian_us\tp90_us\tn\n' "$1" >"$t/expected.tsv"; }
exp() { printf '%s\t%s\t%s\t51\n' "$1" "$2" "$2" >>"$t/expected.tsv"; }

# row <round> <key> <us> -- one candidate sample, appended to $t/samples.tsv
row() { printf '%s\tB\t%s\t%s\n' "$1" "$2" "$3" >>"$t/samples.tsv"; }

# const <key> <rounds> <us> -- the same candidate value, every round
const() { for r in $(seq 1 "$2"); do row "$r" "$1" "$3"; done; }

# split <key> <n> <k> <hi> <lo> -- the first k rounds get hi, the rest lo
split() {
	for r in $(seq 1 "$2"); do
		if [ "$r" -le "$3" ]; then row "$r" "$1" "$4"; else row "$r" "$1" "$5"; fi
	done
}

run() { "$script" "$t/expected.tsv" "$t/samples.tsv" "${1:-/dev/null}" "$t/out"; }

case_no_change() {
	expected v0.3.0; exp wf1/job 1000
	: >"$t/samples.tsv"; const wf1/job 11 1000
	run
}

case_regression() {
	expected v0.3.0; exp wf1/job 6667
	: >"$t/samples.tsv"; const wf1/job 11 9667 # +45%, +3ms, every round
	run && return 1
	grep -qF '`wf1/job`' "$t/out/report.md"
}

case_below_floor() {
	expected v0.3.0; exp wf1/job 200
	: >"$t/samples.tsv"; const wf1/job 11 1700 # +750% ratio, only +1.5ms (below the 2ms step floor)
	run
}

case_not_significant() {
	expected v0.3.0; exp wf1/job 1000
	# half the rounds at 5200 (above), half at 999 (below): median 3099.5, ratio 3.1 and delta
	# 2099.5us clear the gates, but only 5 of 10 rounds are above the stored median.
	: >"$t/samples.tsv"; split wf1/job 10 5 5200 999
	run
}

case_allow_versioned() {
	printf 'v0.3.0 wf1/* 2.0  # known, expires when the expected values are re-measured\n' >"$t/allow.txt"
	: >"$t/samples.tsv"; const wf1/job 11 9667
	expected v0.3.0; exp wf1/job 6667
	run "$t/allow.txt" || return 1 # the matching version suppresses it
	expected v0.4.0; exp wf1/job 6667
	run "$t/allow.txt" && return 1 # re-measured values: the line no longer applies
	return 0
}

case_one_sided_keys() {
	expected v0.3.0; exp wfgone/job 1000
	: >"$t/samples.tsv"; const wfnew/job 2 1000
	run || return 1
	grep -qF $'wfgone/job\tgone' "$t/out/report.tsv" &&
		grep -qF $'wfnew/job\tnew' "$t/out/report.tsv"
}

# Hand-verified critical values for the one-sided sign test at alpha = 0.01 (a single
# gated key, so no Bonferroni correction): n=11 -> 10, n=31 -> 23, n=51 -> 35.
case_sign_test_table() {
	expected v0.3.0; exp wf1/job 1000
	for pair in "11 10" "31 23" "51 35"; do
		set -- $pair
		n=$1 crit=$2
		: >"$t/samples.tsv"; split wf1/job "$n" "$crit" 4000 900
		run && { echo "n=$n k=$crit (at the critical value): expected a regression" >&2; return 1; }
		: >"$t/samples.tsv"; split wf1/job "$n" $((crit - 1)) 4000 900
		run || { echo "n=$n k=$((crit - 1)) (below the critical value): expected no regression" >&2; return 1; }
	done
}

case_no_version() {
	printf 'key\tmedian_us\tp90_us\tn\nwf1/job\t1000\t1000\t51\n' >"$t/expected.tsv"
	: >"$t/samples.tsv"; const wf1/job 3 1000
	run 2>/dev/null
	[ $? -eq 3 ]
}

cases=${*:-no_change regression below_floor not_significant allow_versioned one_sided_keys sign_test_table no_version}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
