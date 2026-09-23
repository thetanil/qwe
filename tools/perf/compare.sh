#!/bin/sh
# usage: compare.sh <baseline-version> <samples.tsv> <allow-list> <outdir>
#
# A/B performance gate. samples.tsv has TSV lines `round  bin  key  us`, bin one of A, B or
# no-baseline (the candidate ran but the baseline could not, for that workflow). Keys look
# like <workflow>/wall, <workflow>/<job> or <workflow>/<job>/<step-index>.
#
# For every key seen in both A and B ("gated"), the A median, the B median, B's p90, the
# ratio and delta of the two medians, and the paired one-sided sign test (rounds where B > A,
# ties dropped) decide whether it regressed. A key with samples in only one bin is "gone" or
# "new"; a no-baseline key is reported too. None of the three are ever gated.
#
# allow-list is a file of lines `<baseline-version> <key-glob> <max-ratio>  # reason` (may be
# missing or empty). A line only suppresses a regression when the baseline version equals its
# version and the observed ratio is at or below its max-ratio, so an accepted slowdown expires
# the moment the baseline moves to a new release.
#
# Writes <outdir>/report.md (verdict, then a per-workflow wall table, then per-key details in
# a <details> block, every duration in ms) and <outdir>/report.tsv (raw microseconds). Exits 1
# if any key regressed after allow-list suppression.
set -eu

# Thresholds. A key regresses only when all three hold.
RATIO_THRESHOLD=1.20
DELTA_FLOOR_STEP_US=500
DELTA_FLOOR_WALL_US=2000
ALPHA=0.01

[ $# -eq 4 ] || {
	echo "usage: compare.sh <baseline-version> <samples.tsv> <allow-list> <outdir>" >&2
	exit 3
}
baseline_version=$1 samples=$2 allow=$3 outdir=$4
[ -f "$samples" ] || { echo "compare: no such file: $samples" >&2; exit 3; }

mkdir -p "$outdir"
allow_file=$allow
[ -f "$allow_file" ] || allow_file=/dev/null

awk -F'\t' -v baseline_version="$baseline_version" \
	-v ratio_threshold="$RATIO_THRESHOLD" \
	-v delta_floor_step="$DELTA_FLOOR_STEP_US" \
	-v delta_floor_wall="$DELTA_FLOOR_WALL_US" \
	-v alpha="$ALPHA" \
	-v allow_file="$allow_file" \
	-v md_out="$outdir/report.md" \
	-v tsv_out="$outdir/report.tsv" '
function sortnum(a, n,    i, j, tmp) {
	for (i = 2; i <= n; i++) {
		tmp = a[i]; j = i - 1
		while (j >= 1 && a[j] > tmp) { a[j + 1] = a[j]; j-- }
		a[j + 1] = tmp
	}
}
function median(vals, n,    tmp, i) {
	if (n == 0) return 0
	for (i = 1; i <= n; i++) tmp[i] = vals[i]
	sortnum(tmp, n)
	if (n % 2 == 1) return tmp[(n + 1) / 2]
	return (tmp[n / 2] + tmp[n / 2 + 1]) / 2
}
function p90(vals, n,    tmp, i, idx) {
	if (n == 0) return 0
	for (i = 1; i <= n; i++) tmp[i] = vals[i]
	sortnum(tmp, n)
	idx = int(0.9 * n)
	if (idx < 0.9 * n) idx++      # ceil, without relying on a ceil() builtin
	if (idx < 1) idx = 1
	if (idx > n) idx = n
	return tmp[idx]
}
# Smallest k such that P(X >= k) <= a, X ~ Binomial(n, 0.5): the one-sided sign-test
# critical value. The binomial tail is summed directly (no lookup table).
function sign_crit(n, a,    c, i, cum, total, k, best) {
	if (n <= 0) return n + 1
	c[0] = 1
	for (i = 1; i <= n; i++) c[i] = c[i - 1] * (n - i + 1) / i
	total = 2 ^ n
	cum = 0
	best = n + 1
	for (k = n; k >= 0; k--) {
		cum += c[k]
		if (cum / total <= a) best = k
		else break
	}
	return best
}
# A glob (only "*" is special) as an anchored ERE.
function glob_re(g,    r, i, c) {
	r = ""
	for (i = 1; i <= length(g); i++) {
		c = substr(g, i, 1)
		if (c == "*") r = r ".*"
		else if (c ~ /[][.^$()|+?{}\\]/) r = r "\\" c
		else r = r c
	}
	return "^" r "$"
}
function fmt_us_ms(us) {
	if (us == "-") return "-"
	return sprintf("%.1f", us / 1000.0)
}
function fmt_num(x) {
	if (x == "-") return "-"
	return sprintf("%.3f", x)
}

BEGIN {
	OFS = "\t"
	nallow = 0
	while ((getline line < allow_file) > 0) {
		sub(/#.*/, "", line)
		gsub(/^[ \t]+|[ \t]+$/, "", line)
		if (line == "") continue
		n = split(line, f, /[ \t]+/)
		if (n < 3) continue
		nallow++
		allow_ver[nallow] = f[1]
		allow_glob[nallow] = f[2]
		allow_re[nallow] = glob_re(f[2])
		allow_max[nallow] = f[3] + 0
	}
	close(allow_file)
	nkeys = 0
}
{
	round = $1 + 0; bin = $2; key = $3; us = $4 + 0
	if (!(key in seen)) { seen[key] = 1; keyorder[++nkeys] = key }
	if (bin == "no-baseline") {
		nobase[key] = 1
		next
	}
	if (bin != "A" && bin != "B") next
	if (round > (maxround[key] + 0)) maxround[key] = round
	if (bin == "A") {
		has_a[key] = 1
		if (!((key, round) in a_round)) { a_round[key, round] = us; an[key]++; a_vals[key, an[key]] = us }
	} else {
		has_b[key] = 1
		if (!((key, round) in b_round)) { b_round[key, round] = us; bn[key]++; b_vals[key, bn[key]] = us }
	}
}
END {
	ngated = 0
	for (ki = 1; ki <= nkeys; ki++) {
		key = keyorder[ki]
		if (key in nobase) { status[key] = "no-baseline"; continue }
		if (has_a[key] && !has_b[key]) { status[key] = "gone"; continue }
		if (!has_a[key] && has_b[key]) { status[key] = "new"; continue }
		status[key] = "gated"; ngated++
	}
	key_alpha = (ngated > 0) ? alpha / ngated : alpha

	any_regression = 0
	nregressed = 0

	for (ki = 1; ki <= nkeys; ki++) {
		key = keyorder[ki]
		if (status[key] != "gated") {
			res_amed[key] = "-"; res_bmed[key] = "-"; res_bp90[key] = "-"
			res_ratio[key] = "-"; res_delta[key] = "-"; res_k[key] = "-"; res_n[key] = "-"
			res_regressed[key] = 0; res_note[key] = ""
			continue
		}
		# a_vals/b_vals are keyed by (key, i); pull this key slice into plain
		# 1-indexed arrays for median()/p90().
		delete av; delete bv
		for (i = 1; i <= an[key]; i++) av[i] = a_vals[key, i]
		for (i = 1; i <= bn[key]; i++) bv[i] = b_vals[key, i]
		amed = median(av, an[key])
		bmed = median(bv, bn[key])
		bp90 = p90(bv, bn[key])
		if (amed > 0) ratio = bmed / amed
		else ratio = (bmed > 0) ? 999999 : 1
		delta = bmed - amed

		pn = 0; keff = 0; kgt = 0
		for (r = 1; r <= maxround[key]; r++) {
			if (((key, r) in a_round) && ((key, r) in b_round)) {
				pn++
				av2 = a_round[key, r]; bv2 = b_round[key, r]
				if (bv2 > av2) { keff++; kgt++ }
				else if (bv2 < av2) keff++
				# ties: counted in pn, not in keff or kgt
			}
		}
		crit = sign_crit(keff, key_alpha)
		significant = (keff > 0 && kgt >= crit)

		floor = (key ~ /\/wall$/) ? delta_floor_wall : delta_floor_step
		regressed = (ratio > ratio_threshold && delta > floor && significant) ? 1 : 0

		note = ""
		if (regressed) {
			for (i = 1; i <= nallow; i++) {
				if (allow_ver[i] == baseline_version && key ~ allow_re[i] && ratio <= allow_max[i]) {
					regressed = 0
					note = "allowed: " allow_glob[i] " (max " allow_max[i] "x, " allow_ver[i] ")"
					break
				}
			}
		}

		res_amed[key] = amed; res_bmed[key] = bmed; res_bp90[key] = bp90
		res_ratio[key] = ratio; res_delta[key] = delta
		res_k[key] = kgt; res_n[key] = pn
		res_regressed[key] = regressed; res_note[key] = note
		if (regressed) { any_regression = 1; regressed_keys[++nregressed] = key }
	}

	# report.tsv: one row per key, raw microseconds.
	print "key\tstatus\ta_median_us\tb_median_us\tb_p90_us\tratio\tdelta_us\tb_gt_a\tn_paired\tregressed\tnote" > tsv_out
	for (ki = 1; ki <= nkeys; ki++) {
		key = keyorder[ki]
		print key, status[key], res_amed[key], res_bmed[key], res_bp90[key], \
			fmt_num(res_ratio[key]), res_delta[key], res_k[key], res_n[key], \
			(status[key] == "gated" ? res_regressed[key] : "-"), res_note[key] > tsv_out
	}
	close(tsv_out)

	# report.md
	print "# perf compare" > md_out
	print "" >> md_out
	if (any_regression) {
		line = "**FAIL** -- regression in:"
		for (i = 1; i <= nregressed; i++) line = line " `" regressed_keys[i] "`" (i < nregressed ? "," : "")
		print line >> md_out
	} else {
		print "**PASS** -- no key regressed" >> md_out
	}
	print "" >> md_out

	print "## Workflow wall times (ms)" >> md_out
	print "" >> md_out
	print "| workflow | status | A median | B median | B p90 | ratio | delta | rounds (B>A/n) | verdict |" >> md_out
	print "|---|---|---|---|---|---|---|---|---|" >> md_out
	for (ki = 1; ki <= nkeys; ki++) {
		key = keyorder[ki]
		if (key !~ /\/wall$/) continue
		wf = key; sub(/\/wall$/, "", wf)
		verdict = (status[key] == "gated") ? (res_regressed[key] ? "FAIL" : "pass") : status[key]
		rn = (status[key] == "gated") ? (res_k[key] "/" res_n[key]) : "-"
		printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", \
			wf, status[key], fmt_us_ms(res_amed[key]), fmt_us_ms(res_bmed[key]), fmt_us_ms(res_bp90[key]), \
			fmt_num(res_ratio[key]), fmt_us_ms(res_delta[key]), rn, verdict >> md_out
	}
	print "" >> md_out

	print "<details><summary>Per-job / per-step details</summary>" >> md_out
	print "" >> md_out
	print "| key | status | A median | B median | B p90 | ratio | delta | rounds (B>A/n) | verdict | note |" >> md_out
	print "|---|---|---|---|---|---|---|---|---|---|" >> md_out
	for (ki = 1; ki <= nkeys; ki++) {
		key = keyorder[ki]
		if (key ~ /\/wall$/) continue
		verdict = (status[key] == "gated") ? (res_regressed[key] ? "FAIL" : "pass") : status[key]
		rn = (status[key] == "gated") ? (res_k[key] "/" res_n[key]) : "-"
		printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", \
			key, status[key], fmt_us_ms(res_amed[key]), fmt_us_ms(res_bmed[key]), fmt_us_ms(res_bp90[key]), \
			fmt_num(res_ratio[key]), fmt_us_ms(res_delta[key]), rn, verdict, res_note[key] >> md_out
	}
	print "" >> md_out
	print "</details>" >> md_out
	close(md_out)

	exit any_regression ? 1 : 0
}
' "$samples"
