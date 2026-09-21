#!/bin/sh
# usage: ours_test.sh <ours.sh>: third_party/ records are dropped, src/ and plugins/ ones kept whole
ours=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT
printf 'SF:src/a.c\nDA:1,1\nLF:1\nLH:1\nend_of_record\nSF:third_party/tinycbor/src/x.c\nDA:1,0\nLF:1\nLH:0\nend_of_record\nSF:plugins/p.lua\nLF:2\nLH:1\nend_of_record\n' >"$t/in"
"$ours" "$t/in" >"$t/out" || exit 1
[ "$(grep -c '^SF:' "$t/out")" = 2 ] || { echo "want 2 records" >&2; cat "$t/out" >&2; exit 1; }
grep -q '^SF:third_party' "$t/out" && { echo "third_party kept" >&2; exit 1; }
grep -q '^SF:src/a.c' "$t/out" && grep -q '^DA:1,1' "$t/out" && grep -q '^SF:plugins/p.lua' "$t/out" || { echo "ours dropped" >&2; exit 1; }
