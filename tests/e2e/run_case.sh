#!/bin/sh
# usage: run_case.sh <qwe-binary> <case-dir>
#
# Case dir layout:
#   args              one argument per line (optional)
#   expected/stdout   golden stdout   (optional; checked if present)
#   expected/stderr   golden stderr   (optional; checked if present)
#   expected/exit     expected exit code (default 0)
#   expected/<path>   any other file: compared with <path> in the work dir
#   everything else   input files, copied to a scratch work dir where qwe runs
#
# Exit: 0 pass, 1 golden mismatch, 3 harness error.
qwe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
case_dir=$2
[ -x "$qwe" ] || { echo "run_case: no binary $qwe" >&2; exit 3; }
[ -d "$case_dir" ] || { echo "run_case: no case dir $case_dir (pwd $(pwd))" >&2; exit 3; }

work=$(mktemp -d) || exit 3
trap 'rm -rf "$work"' EXIT
cp -R "$case_dir"/. "$work"/ || exit 3
rm -rf "$work/expected" "$work/args"

set --
if [ -f "$case_dir/args" ]; then
	while IFS= read -r line; do set -- "$@" "$line"; done < "$case_dir/args"
fi

want_exit=0
[ -f "$case_dir/expected/exit" ] && want_exit=$(cat "$case_dir/expected/exit")

(cd "$work" && "$qwe" "$@" >"$work.stdout" 2>"$work.stderr")
got_exit=$?

fail=0
if [ "$got_exit" != "$want_exit" ]; then
	echo "exit code: want $want_exit, got $got_exit" >&2
	fail=1
fi
check() { # <golden> <actual> <label>
	if ! diff -u "$1" "$2" >&2; then
		echo "mismatch: $3" >&2
		fail=1
	fi
}
[ -f "$case_dir/expected/stdout" ] && check "$case_dir/expected/stdout" "$work.stdout" stdout
[ -f "$case_dir/expected/stderr" ] && check "$case_dir/expected/stderr" "$work.stderr" stderr
(cd "$case_dir/expected" && find . -type f ! -name stdout ! -name stderr ! -name exit) |
while IFS= read -r f; do
	check "$case_dir/expected/$f" "$work/$f" "$f" || true
done
rm -f "$work.stdout" "$work.stderr"
[ "$fail" = 0 ]
