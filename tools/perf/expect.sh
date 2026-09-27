#!/bin/sh
# usage: expect.sh [--note TEXT]... <version> <out.tsv> <samples.tsv>[:bin,bin...]...
#
# Pools timing samples into the stored expected values the perf gate compares against
# (docs/adr/0016-perf-gate-compares-with-stored-expectations.md). Each input is a
# samples.tsv written by tools/perf/run.sh (`round  bin  key  us`), optionally followed by
# ":" and the bin labels to take from it (default: B, the candidate). Older A/B files hold
# the binary of interest under A, B or no-baseline depending on which side it ran on; the
# label list says which.
#
# Writes <out.tsv>:
#   # version: <version>       the label compare.sh reports and the allow-list matches
#   # <note>                    one line per --note (where the samples came from, runners)
#   # source: <file> bins=<..> rows=<n>
#   key  median_us  p90_us  n  (tab-separated, one line per key, in first-seen order)
#
# Median and p90 use the same definitions as compare.sh. Exits 3 on a usage error or an
# input that does not exist, 1 if the inputs hold no matching sample at all.
set -eu

notes=""
while [ $# -gt 0 ] && [ "$1" = "--note" ]; do
	[ $# -ge 2 ] || { echo "expect: --note needs a value" >&2; exit 3; }
	notes="$notes# $2
"
	shift 2
done
[ $# -ge 3 ] || {
	echo "usage: expect.sh [--note TEXT]... <version> <out.tsv> <samples.tsv>[:bin,bin...]..." >&2
	exit 3
}
version=$1 out=$2
shift 2

tmp=$(mktemp) || exit 3
trap 'rm -f "$tmp"' EXIT
header="# version: $version
$notes"
for arg in "$@"; do
	case $arg in
	*:*) file=${arg%:*} bins=${arg##*:} ;;
	*) file=$arg bins=B ;;
	esac
	[ -f "$file" ] || { echo "expect: no such file: $file" >&2; exit 3; }
	before=$(wc -l <"$tmp")
	awk -F'\t' -v bins="$bins" '
		BEGIN { n = split(bins, b, ","); for (i = 1; i <= n; i++) want[b[i]] = 1 }
		NF >= 4 && ($2 in want) { print $3 "\t" $4 }
	' "$file" >>"$tmp"
	rows=$(($(wc -l <"$tmp") - before))
	header="$header# source: $(basename "$(dirname "$file")")/$(basename "$file") bins=$bins rows=$rows
"
done
[ -s "$tmp" ] || { echo "expect: no samples matched" >&2; exit 1; }

{
	printf '%s' "$header"
	awk -F'\t' '
	function sortnum(a, n,    i, j, t) {
		for (i = 2; i <= n; i++) {
			t = a[i]; j = i - 1
			while (j >= 1 && a[j] > t) { a[j + 1] = a[j]; j-- }
			a[j + 1] = t
		}
	}
	{
		if (!($1 in cnt)) order[++nk] = $1
		cnt[$1]++
		v[$1, cnt[$1]] = $2 + 0
	}
	END {
		print "key\tmedian_us\tp90_us\tn"
		for (k = 1; k <= nk; k++) {
			key = order[k]; n = cnt[key]
			delete a
			for (i = 1; i <= n; i++) a[i] = v[key, i]
			sortnum(a, n)
			med = (n % 2 == 1) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
			idx = int(0.9 * n); if (idx < 0.9 * n) idx++
			if (idx < 1) idx = 1
			if (idx > n) idx = n
			printf "%s\t%s\t%s\t%d\n", key, med, a[idx], n
		}
	}' "$tmp"
} >"$out"
