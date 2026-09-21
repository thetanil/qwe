#!/bin/sh
# usage: badge.sh <lcov file>
# Prints a shields.io endpoint document for line coverage of src/ and plugins/ (C and Lua) in an
# lcov file, third_party/ excluded. Red below 70 %, yellow below 85 %, green from there.
# Used by coverage.yml, which publishes it next to the HTML report as coverage.json.
report=$1
[ -f "$report" ] || { echo "usage: badge.sh <lcov file>" >&2; exit 3; }
awk -F: '
/^SF:/ { f = $2 }
/^LF:/ { lf = $2 }
/^LH:/ { lh = $2 }
/^end_of_record/ { if (f ~ /^(src|plugins)\//) { found += lf; hit += lh } f = "" }
END {
	if (found == 0) { print "badge: no src/ or plugins/ lines in the report" > "/dev/stderr"; exit 1 }
	pct = 100 * hit / found
	color = pct < 70 ? "red" : (pct < 85 ? "yellow" : "green")
	printf "{\"schemaVersion\":1,\"label\":\"line coverage\",\"message\":\"%.1f%%\",\"color\":\"%s\"}\n", pct, color
}' "$report"
