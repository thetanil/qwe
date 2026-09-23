#!/bin/sh
# usage: smoke_workflows_test.sh <qwe-binary> <smoke-dir>
#
# Runs `qwe validate` then `qwe run` on every <smoke-dir>/*.yml except neg_*.yml (those are
# negative cases, handled below). Both must exit 0. Timing is not checked. If <smoke-dir>/.qwe
# exists (project plugin fixtures, e.g. smoke_project_plugin.yml's smoke.touch), it is copied
# alongside every workflow, since project plugins load from next to the workflow file. Then
# runs gen_secrets.sh, the same generator script the smoke GitHub job uses, against the given
# (fastbuild) binary: it needs no root or real apt, so it is not skipped here.
#
# A workflow that needs root or a real package install (become, apt) carries a line
# "# smoke: skip-in-bazel: <reason>" near its top and is skipped here; it still runs for real
# in the smoke GitHub job, which has no sandbox to worry about.
#
# A neg_*.yml, or a neg_*/ directory with its own w.yaml, carrying a line
# "# smoke: expect-fail: <message>" is run here too (unlike a plain neg_*.yml, which needs
# GitHub-only continue-on-error machinery to assert against and is left to the smoke GitHub
# job): `qwe validate` must fail with <message> in its output, or, if validate passes,
# `qwe run --summary` must fail with <message> in the summary (reason: timeout and
# dependency-failed show there, not on stdout). A neg_*.yml without that marker is skipped
# here, same as before.
#
# Exit: 0 if every non-skipped case behaved as expected, and gen_secrets.sh found no leak;
# 1 if any did not; 3 harness error.
qwe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
dir=$2
[ -x "$qwe" ] || { echo "smoke_workflows_test: no binary $qwe" >&2; exit 3; }
[ -d "$dir" ] || { echo "smoke_workflows_test: no dir $dir" >&2; exit 3; }

fail=0

# Runs validate, then (only if that passed) run --summary, in $1 (a scratch dir already
# holding the workflow at $2 and anything it needs); expects one of the two to fail with
# $4 somewhere in its output. $3 is the label PASS/FAIL is printed under (the file name, or
# a directory-based case's directory name). Prints PASS/FAIL itself; never sets $fail on
# its own (callers do, from its exit status).
expect_fail() {
	work=$1 name=$2 label=$3 message=$4
	(cd "$work" && "$qwe" validate "$name") >"$work/out" 2>&1
	if [ $? -ne 0 ]; then
		if grep -qF "$message" "$work/out"; then
			echo "PASS: $label (validate)"
			return 0
		fi
		echo "FAIL: $label: validate failed, but did not mention '$message':" >&2
		cat "$work/out" >&2
		return 1
	fi
	(cd "$work" && "$qwe" run "$name" --summary summary.md) >"$work/out" 2>&1
	rc=$?
	cat "$work/summary.md" >>"$work/out" 2>/dev/null
	if [ $rc -eq 0 ]; then
		echo "FAIL: $label: expected to fail (validate and run both succeeded)" >&2
		return 1
	fi
	if grep -qF "$message" "$work/out"; then
		echo "PASS: $label (run)"
		return 0
	fi
	echo "FAIL: $label: run failed, but neither its output nor its summary mentioned '$message':" >&2
	cat "$work/out" >&2
	return 1
}

for f in "$dir"/*.yml; do
	name=$(basename "$f")
	case "$name" in
		neg_*)
			marker=$(grep -m1 '^# smoke: expect-fail:' "$f" || true)
			[ -n "$marker" ] || continue
			message=${marker#*expect-fail: }
			work=$(mktemp -d) || exit 3
			cp "$f" "$work/"
			expect_fail "$work" "$name" "$name" "$message" || fail=1
			rm -rf "$work"
			continue
			;;
	esac
	skip=$(grep '^# smoke: skip-in-bazel' "$f")
	if [ -n "$skip" ]; then
		echo "SKIP: $name: $skip"
		continue
	fi
	work=$(mktemp -d) || exit 3
	cp "$f" "$work/"
	[ -d "$dir/.qwe" ] && cp -R "$dir/.qwe" "$work/"
	if ! (cd "$work" && "$qwe" validate "$name"); then
		echo "FAIL: $name: qwe validate" >&2
		fail=1
	elif ! (cd "$work" && "$qwe" run "$name"); then
		echo "FAIL: $name: qwe run" >&2
		fail=1
	else
		echo "PASS: $name"
	fi
	rm -rf "$work"
done

# Directory-based negatives: neg_*/ with its own w.yaml and (typically) its own .qwe/plugins/.
for d in "$dir"/neg_*/; do
	[ -d "$d" ] || continue
	name=$(basename "$d")
	wf="$d/w.yaml"
	if [ ! -f "$wf" ]; then
		echo "FAIL: $name: no w.yaml" >&2
		fail=1
		continue
	fi
	marker=$(grep -m1 '^# smoke: expect-fail:' "$wf" || true)
	if [ -z "$marker" ]; then
		echo "FAIL: $name: no '# smoke: expect-fail:' marker in w.yaml" >&2
		fail=1
		continue
	fi
	message=${marker#*expect-fail: }
	work=$(mktemp -d) || exit 3
	cp -R "$d"/. "$work/"
	expect_fail "$work" w.yaml "$name" "$message" || fail=1
	rm -rf "$work"
done

if ! "$dir/gen_secrets.sh" "$qwe" "$dir"; then
	echo "FAIL: gen_secrets.sh" >&2
	fail=1
fi

exit $fail
