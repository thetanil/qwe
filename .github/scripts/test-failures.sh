#!/bin/bash
# usage: .github/scripts/test-failures.sh [lines]
#
# After a bazel test (or bazel coverage) step failed: says which tests failed and why, where it
# can be read. The runner and its test.log files are gone once the job ends, and bazel's own
# last word is "see <path>/test.log", so for each failed test this
#   - prints the last <lines> (default 60) of its test.log here,
#   - adds an error annotation with the last 20 (shown on the run's page, no log to open),
#   - adds its log's end to the job summary.
# A failed test is one whose test.xml has an <error> or <failure> (bazel writes one per test,
# per run and shard). Exits 1 if it found any, so this is the step a failed job opens on and the
# one `gh run view --log-failed` prints; 0 if none (the failure was elsewhere: a build error).
set -u
lines=${1:-60}
out=$(bazel info output_path 2>/dev/null) || { echo "test-failures: no bazel output path" >&2; exit 0; }

# %, CR and LF must be escaped in a workflow command's message.
escape() { sed -e 's/%/%25/g' -e 's/\r/%0D/g' | awk 'NR > 1 { printf "%%0A" } { printf "%s", $0 }'; }
# test.log without the three lines bazel puts on top of every one.
body() { sed -e '1,3{/^exec \${PAGER/d;/^Executing tests from /d;/^-\{20,\}$/d}' "$1" 2>/dev/null || echo "(no test.log: $1)"; }

failed=0
seen=" "
while IFS= read -r xml; do
	grep -qE '<(error|failure)[ >]' "$xml" || continue
	dir=$(dirname "$xml")
	rel=${dir#*/testlogs/}
	rel=$(echo "$rel" | sed -E 's#/(run_[0-9]+_of_[0-9]+|shard_[0-9]+_of_[0-9]+)##g')
	target="//${rel%/*}:${rel##*/}"
	log=$dir/test.log
	# one report per target: with --runs_per_test or shards, the first failing run stands for all
	case $seen in *" $target "*) continue ;; esac
	seen="$seen$target "
	failed=$((failed + 1))

	echo
	echo "==================== FAILED: $target (last $lines lines of test.log) ===================="
	body "$log" | tail -n "$lines"

	if [ "${GITHUB_ACTIONS-}" = true ]; then
		echo "::error title=FAILED: $target::$(body "$log" | tail -n 20 | escape)"
	fi
	if [ -n "${GITHUB_STEP_SUMMARY-}" ]; then
		{
			[ "$failed" -eq 1 ] && printf '### Failed tests\n\n'
			printf '<details open><summary><code>%s</code></summary>\n\n```\n' "$target"
			body "$log" | tail -n "$lines" | sed 's/```/` ` `/g'
			printf '```\n</details>\n\n'
		} >>"$GITHUB_STEP_SUMMARY"
	fi
done < <(find "$out"/*/testlogs -name test.xml 2>/dev/null | sort)

if [ "$failed" -eq 0 ]; then
	echo "test-failures: no failed test in the test logs; the failure was elsewhere (look for a build error above)"
	exit 0
fi
echo
echo "test-failures: $failed failed test(s), listed above"
exit 1
