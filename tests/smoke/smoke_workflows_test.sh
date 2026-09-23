#!/bin/sh
# usage: smoke_workflows_test.sh <qwe-binary> <smoke-dir>
#
# Runs `qwe validate` then `qwe run` on every <smoke-dir>/*.yml except neg_*.yml (those are
# negative cases: the smoke GitHub job asserts they fail, with GitHub-only machinery this
# harness does not have). Both must exit 0. Timing is not checked. Then runs
# gen_secrets.sh, the same generator script the smoke GitHub job uses, against the given
# (fastbuild) binary: it needs no root or real apt, so it is not skipped here.
#
# A workflow that needs root or a real package install (become, apt) carries a line
# "# smoke: skip-in-bazel: <reason>" near its top and is skipped here; it still runs for real
# in the smoke GitHub job, which has no sandbox to worry about.
#
# Exit: 0 if every non-skipped workflow validated and ran, and gen_secrets.sh found no
# leak; 1 if any did not; 3 harness error.
qwe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
dir=$2
[ -x "$qwe" ] || { echo "smoke_workflows_test: no binary $qwe" >&2; exit 3; }
[ -d "$dir" ] || { echo "smoke_workflows_test: no dir $dir" >&2; exit 3; }

fail=0
for f in "$dir"/*.yml; do
	name=$(basename "$f")
	case "$name" in neg_*) continue ;; esac
	skip=$(grep '^# smoke: skip-in-bazel' "$f")
	if [ -n "$skip" ]; then
		echo "SKIP: $name: $skip"
		continue
	fi
	work=$(mktemp -d) || exit 3
	cp "$f" "$work/"
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

if ! "$dir/gen_secrets.sh" "$qwe" "$dir"; then
	echo "FAIL: gen_secrets.sh" >&2
	fail=1
fi

exit $fail
