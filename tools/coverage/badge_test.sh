#!/bin/bash
# usage: badge_test.sh <badge.sh> [case...]     Cases: src_and_plugins_only thresholds
badge=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT

# rec <path> <lines found> <lines hit>
rec() { printf 'SF:%s\nLF:%s\nLH:%s\nend_of_record\n' "$1" "$2" "$3"; }

case_src_and_plugins_only() {
	{
		rec src/kernel/a.c 100 90
		rec plugins/builtin/x.lua 100 70
		rec third_party/luajit/big.c 100000 0 # must not count
	} >"$t/r.dat"
	got=$("$badge" "$t/r.dat") || return 1
	want='{"schemaVersion":1,"label":"line coverage","message":"80.0%","color":"yellow"}'
	[ "$got" = "$want" ] || { echo "got $got" >&2; return 1; }
}

case_thresholds() {
	# lines hit out of 1000 -> colour
	for row in "699 red" "700 yellow" "849 yellow" "850 green" "1000 green"; do
		set -- $row
		rec src/a.c 1000 "$1" >"$t/r.dat"
		got=$("$badge" "$t/r.dat") || return 1
		case $got in *"\"color\":\"$2\""*) ;; *) echo "$1/1000: want $2, got $got" >&2; return 1 ;; esac
	done
	rec third_party/a.c 10 10 >"$t/r.dat"
	"$badge" "$t/r.dat" 2>/dev/null && { echo "a report with no src/ lines must fail" >&2; return 1; }
	return 0
}

cases=${*:-src_and_plugins_only thresholds}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
