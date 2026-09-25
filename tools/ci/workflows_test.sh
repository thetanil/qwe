#!/bin/bash
# usage: workflows_test.sh [case...]     (no argument: every case)
#
# Cases: badge_missing badge_dangling command_unwired trigger_missing nightly_calls_every_gate release_calls_every_gate non_workflow_badge fuzz_manual_only release_calls_smoke nightly_calls_smoke repo_is_consistent.
# The first four build a copy of the repo's CI files, break one thing, and expect
# check_repo to fail; repo_is_consistent runs check_repo on the files as committed.
#
# check_repo <root> rules:
#  1. every .github/workflows/<w>.yml has a badge in README.md, and every badge names a workflow file
#  2. every command in the "Every push" and "On demand" code blocks of docs/ci-checks.md
#     appears in some workflow file
#  4. nightly.yml and release.yml call all five gate workflows (tests asan ubsan valgrind coverage)
#  5. fuzz.yml never starts on a push or a schedule of its own; nightly.yml calls it and release.yml does not
#  3. every workflow but fuzz.yml, release.yml, nightly.yml and codeql.yml runs on workflow_call, and on
#     push to main (valgrind.yml is on demand and must not run on push; codeql.yml is GitHub's code
#     scanning, not a gate, with its own triggers)
#  6. nightly.yml and release.yml both call smoke.yml
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
		case $w in fuzz.yml | release.yml | nightly.yml | codeql.yml) continue ;; esac
		push=$(grep -c 'branches: \[main\]' "$f")
		if [ "$w" = valgrind.yml ]; then want=0; else want=1; fi # valgrind: on demand only
		if ! grep -q 'workflow_call:' "$f" || [ "$push" -ne "$want" ]; then
			echo "rule 3: $w must have workflow_call, and push to main only if it runs on every push" >&2
			bad=1
		fi
	done
	if [ -f .github/workflows/fuzz.yml ] && grep -Eq "^  (push|schedule):" .github/workflows/fuzz.yml; then
		echo "rule 5: fuzz.yml must not start on push or schedule" >&2
		bad=1
	fi
	for caller in nightly release; do
		for g in tests asan ubsan valgrind coverage; do
			if ! grep -q "uses: ./.github/workflows/$g.yml" ".github/workflows/$caller.yml" 2>/dev/null; then
				echo "rule 4: $caller.yml does not call $g.yml" >&2
				bad=1
			fi
		done
	done
	for caller in nightly release; do
		if ! grep -q "uses: ./.github/workflows/smoke.yml" ".github/workflows/$caller.yml" 2>/dev/null; then
			echo "rule 6: $caller.yml does not call smoke.yml" >&2
			bad=1
		fi
	done
	grep -q "uses: ./.github/workflows/fuzz.yml" .github/workflows/nightly.yml 2>/dev/null || { echo "rule 5: nightly.yml does not call fuzz.yml" >&2; bad=1; }
	! grep -q "uses: ./.github/workflows/fuzz.yml" .github/workflows/release.yml 2>/dev/null || { echo "rule 5: release.yml must not call fuzz.yml" >&2; bad=1; }
	for w in $(grep -o 'actions/workflows/[A-Za-z0-9_.-]*\.yml/badge.svg' README.md | sed -e 's|actions/workflows/||' -e 's|/badge.svg||'); do
		if [ ! -f ".github/workflows/$w" ]; then
			echo "rule 1: README badge names $w, which does not exist" >&2
			bad=1
		fi
	done
	cmds=$(awk '/^## / {s = ($0 ~ /^## (Every push|On demand)$/); n = 0; next} s && /^```/ {n++; next} s && n==1 {print}' docs/ci-checks.md)
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

# expect_pass <name> <mutation command run in the fixture>
expect_pass() {
	t=$(fixture)
	(cd "$t" && eval "$2") || exit 3
	if ! (check_repo "$t") 2>/dev/null; then
		echo "$1: check_repo failed a good tree" >&2
		rm -rf "$t"
		return 1
	fi
	rm -rf "$t"
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
case_nightly_calls_every_gate() { expect_fail nightly_calls_every_gate 'sed -i "/valgrind.yml/d" .github/workflows/nightly.yml'; }
case_release_calls_every_gate() { expect_fail release_calls_every_gate 'sed -i "/coverage.yml/d" .github/workflows/release.yml'; }
case_non_workflow_badge() { expect_pass non_workflow_badge 'echo "[![c](https://img.shields.io/endpoint?url=https://o.github.io/r/coverage.json)](https://o.github.io/r/)" >> README.md'; }
case_fuzz_manual_only() {
	expect_fail fuzz_push 'sed -i "s/^  workflow_dispatch:/  push:\n    branches: [main]\n  workflow_dispatch:/" .github/workflows/fuzz.yml' &&
		expect_fail fuzz_schedule 'sed -i "s/^  workflow_dispatch:/  schedule:\n    - cron: \"0 3 * * *\"\n  workflow_dispatch:/" .github/workflows/fuzz.yml' &&
		expect_fail nightly_drops_fuzz 'sed -i "/fuzz.yml/d" .github/workflows/nightly.yml' &&
		expect_fail release_calls_fuzz 'printf "  fuzz:\n    uses: ./.github/workflows/fuzz.yml\n" >> .github/workflows/release.yml'
}
case_release_calls_smoke() { expect_fail release_calls_smoke 'sed -i "/smoke.yml/d" .github/workflows/release.yml'; }
case_nightly_calls_smoke() { expect_fail nightly_calls_smoke 'sed -i "/smoke.yml/d" .github/workflows/nightly.yml'; }
case_repo_is_consistent() { (check_repo "$here"); }

cases=${*:-badge_missing badge_dangling command_unwired trigger_missing nightly_calls_every_gate release_calls_every_gate non_workflow_badge fuzz_manual_only release_calls_smoke nightly_calls_smoke repo_is_consistent}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
