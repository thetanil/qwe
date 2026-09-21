#!/bin/sh
# usage: ours.sh <lcov file>   (writes the filtered report to stdout)
# Drops the records of vendored code (third_party/), which is not ours, so the HTML report and the
# totals in CI cover src/ and plugins/ only.
[ -f "$1" ] || { echo "usage: ours.sh <lcov file>" >&2; exit 3; }
awk 'BEGIN { RS = "end_of_record\n"; ORS = "" } $0 !~ /(^|\n)SF:third_party\// { print $0 "end_of_record\n" }' "$1"
