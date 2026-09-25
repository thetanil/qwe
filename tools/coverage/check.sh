#!/bin/sh
# usage: bazel run //tools/coverage:check
# Runs `bazel coverage //... --combined_report=lcov` and fails if any file of ours (under src/,
# plugins/ or tools/; C and Lua) has fewer than 85% of its lines covered.
set -e
cd "${BUILD_WORKSPACE_DIRECTORY:?run this with bazel run}"
min=85
bazel coverage //... --combined_report=lcov
report=bazel-out/_coverage/_coverage_report.dat
now=$(mktemp)
trap 'rm -f "$now"' EXIT
# path lines-found lines-hit
awk -F: '/^SF:/{f=$2} /^LF:/{lf=$2} /^LH:/{lh=$2} /^end_of_record/{ if (f ~ /^(src|plugins|tools)\//) print f, lf, lh; f="" }' "$report" | sort > "$now"

echo
echo "== Coverage per file (at least $min% of lines)"
status=0
awk -v min="$min" '
{
	f=$1; found=$2; hit=$3
	lang = (f ~ /\.lua$/) ? "Lua" : "C"
	F[lang]+=found; H[lang]+=hit; nfiles[lang]++
	if (found && 100*hit < min*found) { printf "  FAIL  %-48s %5.1f%%  (%d of %d lines)\n", f, 100*hit/found, hit, found; bad++ }
}
END {
	for (l in F) printf "  %-3s  %d of %d lines covered (%.1f%%) in %d files\n", l, H[l], F[l], 100*H[l]/F[l], nfiles[l]
	printf "  files below %d%%: %d\n", min, bad
	exit bad ? 1 : 0
}' "$now" || status=1

if [ "$status" != 0 ]; then
	echo
	echo "== Lines to cover in the failing files"
	awk -v min="$min" '$2 && 100*$3 < min*$2 {print $1}' "$now" | while read -r f; do
		printf '  %s: ' "$f"
		awk -v f="$f" '/^SF:/{on=($0=="SF:"f)} on&&/^DA:/{split(substr($0,4),a,","); if(a[2]==0) printf "%s ", a[1]}' "$report"
		echo
	done
	echo
	echo "check: coverage below $min%" >&2
	exit 1
fi
echo "check: every file is at least $min% covered"
