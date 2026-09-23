#!/bin/sh
# usage: e2e-summary.sh <bazel-test-log>
#
# Parses <bazel-test-log> (bazel test's own stdout+stderr) for every //tests/e2e:<case>_test
# result line and appends a markdown table to $GITHUB_STEP_SUMMARY: one row per case, each
# linking to tests/e2e/<case>/ (or, for the handful of standalone sh_test targets with no
# matching case directory, like require_ssh_test, to their .sh file instead, or no link if
# neither exists) at the exact commit ($GITHUB_SHA, not a branch, so the link keeps working
# after a later rename or delete -- same reasoning as summary-link.sh, and no line numbers
# for the same reason: those would need updating on every edit, a path does not).
#
# A result line that does not match the expected "TARGET  [(cached) ]STATUS in N.Ns" shape
# (bazel's rarer statuses like "NO STATUS" have a space in them) is skipped rather than
# failing this script -- the bazel test step itself is still what fails the job.
set -e
log=$1
: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set (this script only makes sense in a GitHub Actions step)}"
: "${GITHUB_SHA:?GITHUB_SHA is not set}"
: "${GITHUB_STEP_SUMMARY:?GITHUB_STEP_SUMMARY is not set}"
if [ -z "$log" ] || [ ! -f "$log" ]; then
	echo "e2e-summary: usage: e2e-summary.sh <bazel-test-log>" >&2
	exit 3
fi

here=$(cd "$(dirname "$0")/../.." && pwd)
base="https://github.com/$GITHUB_REPOSITORY"

mark() {
	case "$1" in
		PASSED) echo "✅ PASSED" ;;
		FAILED) echo "❌ FAILED" ;;
		TIMEOUT) echo "⏱️ TIMEOUT" ;;
		FLAKY) echo "🍂 FLAKY" ;;
		*) echo "$1" ;;
	esac
}

{
	echo "### tests/e2e"
	echo
	echo "| case | outcome | duration |"
	echo "| --- | --- | --- |"
	grep -E '^//tests/e2e:' "$log" |
		sed -nE 's#^//tests/e2e:([A-Za-z0-9_]+)[[:blank:]]+(\([a-z ]+\)[[:blank:]]+)?([A-Z]+) in ([0-9.]+s)$#\1|\3|\4#p' |
		sort |
		while IFS='|' read -r target status duration; do
			case=${target%_test}
			if [ -d "$here/tests/e2e/$case" ]; then
				cell="[\`$case\`]($base/tree/$GITHUB_SHA/tests/e2e/$case)"
			elif [ -f "$here/tests/e2e/$case.sh" ]; then
				cell="[\`$case\`]($base/blob/$GITHUB_SHA/tests/e2e/$case.sh)"
			elif [ -f "$here/tests/e2e/$target.sh" ]; then
				# a standalone sh_test with no case directory (e.g. require_ssh_test):
				# its own file is named after the full target, not the stripped case.
				cell="[\`$case\`]($base/blob/$GITHUB_SHA/tests/e2e/$target.sh)"
			else
				cell="\`$case\`"
			fi
			echo "| $cell | $(mark "$status") | $duration |"
		done
	echo
} >>"$GITHUB_STEP_SUMMARY"
