#!/bin/bash
# usage: coverage_test.sh [case...]     (no argument: every case)
#
# Cases: missing_plugin unwired_file repo_is_consistent.
# missing_plugin and unwired_file build a copy of tests/smoke and src/kernel/lua/BUILD,
# break one thing, and expect the checks below to catch it; repo_is_consistent runs both
# checks on the files as committed.
#
# check_plugin_coverage <root>: every `plugin.<name>` and `backend.<name>` embedded module
# (src/kernel/lua/BUILD's MODULES table -- the built-in plugin registry) is used by some
# tests/smoke/*.yml or *.yml.in: `uses: <name>` for a step plugin, `run:` for run, `target:
# local` for backend.local. backend.ssh (no ssh in smoke) and backend.recording (test-only)
# are exempt.
#
# check_smoke_wired <root>: every tests/smoke/*.yml and neg_*/ that the generic Bazel run
# (tests/smoke:smoke_workflows_test) cannot itself verify -- a "# smoke: skip-in-bazel:"
# workflow (needs real privilege), or a neg_*(/w.yaml) with no "# smoke: expect-fail:"
# marker (needs GitHub's continue-on-error) -- is named in .github/workflows/smoke.yml. A
# plain workflow or a marked negative needs no explicit mention: the generic Bazel run
# already exercises it, via tests/smoke/BUILD's glob(["*.yml"]).
here=$(cd "$(dirname "$0")/../.." && pwd)

check_plugin_coverage() {
	root=$1
	bad=0
	names=$(grep -oE '"(plugin|backend)\.[A-Za-z0-9_.-]+"' "$root/src/kernel/lua/BUILD" | tr -d '"' | sort -u)
	haystack=$(cat "$root"/tests/smoke/*.yml "$root"/tests/smoke/*.yml.in 2>/dev/null)
	for full in $names; do
		case "$full" in
			backend.ssh | backend.recording) continue ;; # no ssh in smoke; recording is test-only
		esac
		name=${full#*.}
		case "$full" in
			backend.local) needle="target: local" ;;
			plugin.run) needle="run:" ;;
			plugin.*) needle="uses: $name" ;;
			*)
				echo "check_plugin_coverage: $full: no rule for this kind of embedded module" >&2
				bad=1
				continue
				;;
		esac
		if ! printf '%s' "$haystack" | grep -qF "$needle"; then
			echo "rule: no smoke workflow uses $full (looked for '$needle')" >&2
			bad=1
		fi
	done
	return $bad
}

check_smoke_wired() {
	root=$1
	bad=0
	workflows_yml="$root/.github/workflows/smoke.yml"
	for f in "$root"/tests/smoke/*.yml; do
		name=$(basename "$f")
		case "$name" in
			neg_*)
				grep -qm1 '^# smoke: expect-fail:' "$f" && continue
				;;
			*)
				grep -qm1 '^# smoke: skip-in-bazel' "$f" || continue
				;;
		esac
		if ! grep -qF "tests/smoke/$name" "$workflows_yml"; then
			echo "rule: $name is not named in smoke.yml, and the Bazel suite cannot verify it either" >&2
			bad=1
		fi
	done
	for d in "$root"/tests/smoke/neg_*/; do
		[ -d "$d" ] || continue
		name=$(basename "$d")
		wf="$d/w.yaml"
		[ -f "$wf" ] && grep -qm1 '^# smoke: expect-fail:' "$wf" && continue
		if ! grep -qF "$name" "$workflows_yml"; then
			echo "rule: $name/ is not named in smoke.yml, and the Bazel suite cannot verify it either" >&2
			bad=1
		fi
	done
	return $bad
}

# A scratch copy of the files these checks read.
fixture() {
	t=$(mktemp -d) || exit 3
	mkdir -p "$t/src/kernel/lua" "$t/.github/workflows" "$t/tests"
	cp "$here/src/kernel/lua/BUILD" "$t/src/kernel/lua/"
	cp -R "$here/tests/smoke" "$t/tests/"
	cp "$here/.github/workflows/smoke.yml" "$t/.github/workflows/"
	echo "$t"
}

case_missing_plugin() {
	local t got
	t=$(fixture)
	# A module nothing in tests/smoke uses.
	sed -i 's|"plugin.run":|"plugin.nonexistent.thing": "//fake:thing.lua",\n    "plugin.run":|' \
		"$t/src/kernel/lua/BUILD"
	got=0
	check_plugin_coverage "$t" 2>/dev/null || got=$?
	rm -rf "$t"
	if [ "$got" = 0 ]; then
		echo "missing_plugin: check_plugin_coverage passed a broken tree" >&2
		return 1
	fi
}

case_unwired_file() {
	local t got
	t=$(fixture)
	cat >"$t/tests/smoke/smoke_unused.yml" <<-'EOF'
	# smoke: skip-in-bazel: fixture only, never really run
	name: smoke unused
	jobs:
	  j:
	    target: local
	    steps:
	      - run: echo hi
	EOF
	got=0
	check_smoke_wired "$t" 2>/dev/null || got=$?
	rm -rf "$t"
	if [ "$got" = 0 ]; then
		echo "unwired_file: check_smoke_wired passed a broken tree" >&2
		return 1
	fi
}

case_repo_is_consistent() {
	local ok=1
	check_plugin_coverage "$here" || ok=0
	check_smoke_wired "$here" || ok=0
	[ "$ok" = 1 ]
}

cases=${*:-missing_plugin unwired_file repo_is_consistent}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
