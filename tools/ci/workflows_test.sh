#!/bin/bash
# usage: workflows_test.sh [case...]     (no argument: every case)
#
# Cases: badge_missing badge_dangling command_unwired trigger_missing repo_is_consistent.
# The first four build a copy of the repo's CI files, break one thing, and expect
# check_repo to fail; repo_is_consistent runs check_repo on the files as committed.
#
# check_repo <root> rules:
#  1. every .github/workflows/<w>.yml has a badge in README.md, and every badge names a workflow file
#  2. every command in the "Every push" (formerly "Every change") code block of docs/ci-checks.md
#     appears in some workflow file
#  3. every workflow but fuzz.yml and release.yml runs on push to main and on workflow_call
here=$(cd "$(dirname "$0")/../.." && pwd)

check_repo() {
	root=$1
	bad=0
	cd "$root" || return 2
	for f in .github/workflows/*.yml; do
		w=$(basename "$f")
		if ! grep -q "actions/workflows/$w/badge.svg" README.md; then
			echo "rule 1: $w has no badge in README.md" >&2
			bad=1
		fi
		case $w in fuzz.yml | release.yml) continue ;; esac
		if ! grep -q 'workflow_call:' "$f" || ! grep -q 'branches: \[main\]' "$f"; then
			echo "rule 3: $w lacks push to main or workflow_call" >&2
			bad=1
		fi
	done
	for w in $(grep -o 'actions/workflows/[A-Za-z0-9_.-]*\.yml/badge.svg' README.md | sed -e 's|actions/workflows/||' -e 's|/badge.svg||'); do
		if [ ! -f ".github/workflows/$w" ]; then
			echo "rule 1: README badge names $w, which does not exist" >&2
			bad=1
		fi
	done
	cmds=$(awk '/^## Every (change|push)$/ {s=1; next} s && /^```/ {n++; if (n==2) exit; next} s && n==1 {print}' docs/ci-checks.md)
	if [ -z "$cmds" ]; then
		echo "rule 2: no code block under '## Every push' in docs/ci-checks.md" >&2
		bad=1
	fi
	while IFS= read -r c; do
		[ -n "$c" ] || continue
		if ! cat .github/workflows/*.yml | grep -qF -- "$c"; then
			echo "rule 2: '$c' (docs/ci-checks.md) is in no workflow" >&2
			bad=1
		fi
	done <<<"$cmds"
	return $bad
}

# A scratch copy of the CI files to break.
fixture() {
	t=$(mktemp -d) || exit 3
	mkdir -p "$t/.github" "$t/docs"
	cp -RL "$here/.github/workflows" "$t/.github/"
	cp "$here/README.md" "$t/"
	cp "$here/docs/ci-checks.md" "$t/docs/"
	echo "$t"
}

# expect_fail <name> <mutation command run in the fixture>
expect_fail() {
	t=$(fixture)
	(cd "$t" && eval "$2") || exit 3
	if (check_repo "$t") 2>/dev/null; then
		echo "$1: check_repo passed a broken tree" >&2
		rm -rf "$t"
		return 1
	fi
	rm -rf "$t"
}

case_badge_missing() { expect_fail badge_missing 'sed -i "/workflows\/tests.yml\/badge.svg/d" README.md'; }
case_badge_dangling() { expect_fail badge_dangling 'echo "[![x](https://github.com/o/r/actions/workflows/nope.yml/badge.svg)](x)" >> README.md'; }
case_command_unwired() { expect_fail command_unwired 'sed -i "s|bazel test //\.\.\.|bazel test //nothing|" .github/workflows/tests.yml'; }
case_trigger_missing() { expect_fail trigger_missing 'sed -i "/workflow_call:/d" .github/workflows/tests.yml'; }
case_repo_is_consistent() { (check_repo "$here"); }

cases=${*:-badge_missing badge_dangling command_unwired trigger_missing repo_is_consistent}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
