#!/bin/sh
# usage: bazel run //tools/luacov:check [-- --update]
# Runs `bazel coverage //... --combined_report=lcov` and fails if any file under src/ or
# plugins/ (C and Lua) has more uncovered lines than tools/luacov/floor.txt allows.
# It counts misses, not a percentage: new code without tests raises them, new code with
# tests does not, and deleting code cannot fail it.
#   --update   rewrite floor.txt from this run (a ratchet: commit the result deliberately)
set -e
cd "${BUILD_WORKSPACE_DIRECTORY:?run this with bazel run}"
floor=tools/luacov/floor.txt
bazel coverage //... --combined_report=lcov
report=bazel-out/_coverage/_coverage_report.dat
now=$(mktemp)
trap 'rm -f "$now"' EXIT
# path lines-found lines-hit
awk -F: '/^SF:/{f=$2} /^LF:/{lf=$2} /^LH:/{lh=$2} /^end_of_record/{ if (f ~ /^(src|plugins)\//) print f, lf, lh; f="" }' "$report" | sort > "$now"
if [ "$1" = "--update" ]; then
	awk '{print $1, $2-$3}' "$now" > "$floor"
	echo "check: wrote $floor ($(wc -l < "$floor") files)"
	exit 0
fi
[ -f "$floor" ] || { echo "check: no $floor; run with --update" >&2; exit 3; }

echo
echo "== Coverage against $floor (lines uncovered: now / allowed)"
status=0
awk 'NR==FNR{max[$1]=$2; next}
{
	f=$1; found=$2; hit=$3; miss=found-hit
	lang = (f ~ /\.lua$/) ? "Lua" : "C"
	F[lang]+=found; H[lang]+=hit; nfiles[lang]++
	if (!(f in max)) {
		if (miss == 0) { fresh++; next }
		printf "  FAIL  %-48s %4d / (not listed)   new file with untested lines\n", f, miss; bad++; next
	}
	if (miss > max[f]) { printf "  FAIL  %-48s %4d / %-4d          +%d\n", f, miss, max[f], miss-max[f]; bad++ }
	else if (miss < max[f]) { printf "  better %-47s %4d / %-4d          -%d\n", f, miss, max[f], max[f]-miss; better++ }
	else same++
}
END {
	for (l in F) printf "  %-3s  %d of %d lines covered (%.1f%%) in %d files\n", l, H[l], F[l], 100*H[l]/F[l], nfiles[l]
	printf "  files: %d unchanged, %d with fewer uncovered lines, %d new and fully covered, %d with more (failing)\n", same, better, fresh, bad
	if (better) print "  (run with --update to record the lower numbers, so they cannot rise again)"
	exit bad ? 1 : 0
}' "$floor" "$now" || status=1

if [ "$status" != 0 ]; then
	echo
	echo "== Lines to cover in the failing files"
	awk 'NR==FNR{max[$1]=$2; next} ($2-$3) > (($1 in max) ? max[$1] : 0) {print $1}' "$floor" "$now" | while read -r f; do
		printf '  %s: ' "$f"
		awk -v f="$f" '/^SF:/{on=($2=="" && $0=="SF:"f)} on&&/^DA:/{split(substr($0,4),a,","); if(a[2]==0) printf "%s ", a[1]}' "$report"
		echo
	done
	echo
	echo "check: coverage dropped" >&2
	exit 1
fi
echo "check: coverage has not dropped"
