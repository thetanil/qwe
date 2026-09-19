#!/bin/sh
# usage: run_case.sh <qwe-binary> <case-dir>
#
# Case dir layout:
#   args              one argument per line (optional)
#   expected/stdout   golden stdout   (optional; checked if present)
#   expected/stderr   golden stderr   (optional; checked if present)
#   expected/exit     expected exit code (default 0)
#   expected/<path>   any other file: compared with <path> in the work dir
#   check.sh          optional extra assertions, run in the work dir after qwe;
#                     sees $QWE_STDOUT (qwe's stdout) and must exit 0
#   everything else   input files, copied to a scratch work dir where qwe runs
#
#
# If qwe left exactly one .qwe/runs/<id>/, that directory is renamed to RUN/ and
# result.json has its run id and times replaced by RUN and TIME, so goldens can
# say expected/RUN/j.log and expected/RUN/result.json.
#
# Exit: 0 pass, 1 golden mismatch, 3 harness error.
qwe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
case_dir=$2
[ -x "$qwe" ] || { echo "run_case: no binary $qwe" >&2; exit 3; }
[ -d "$case_dir" ] || { echo "run_case: no case dir $case_dir (pwd $(pwd))" >&2; exit 3; }

case_dir=$(cd "$case_dir" && pwd)
work=$(mktemp -d) || exit 3
trap 'rm -rf "$work"' EXIT
cp -R "$case_dir"/. "$work"/ || exit 3
rm -rf "$work/expected" "$work/args"
rm -f "$work/check.sh"

set --
if [ -f "$case_dir/args" ]; then
	while IFS= read -r line; do set -- "$@" "$line"; done < "$case_dir/args"
fi

want_exit=0
[ -f "$case_dir/expected/exit" ] && want_exit=$(cat "$case_dir/expected/exit")

(cd "$work" && "$qwe" "$@" >"$work.stdout" 2>"$work.stderr")
got_exit=$?

# Give the one run directory a fixed name and blank its run id and times.
if [ -d "$work/.qwe/runs" ] && [ "$(ls "$work/.qwe/runs" | wc -l)" = 1 ]; then
	mv "$work/.qwe/runs/"* "$work/RUN"
	if [ -f "$work/RUN/result.json" ]; then
		sed -e 's/"run_id": "[^"]*"/"run_id": "RUN"/' \
		    -e 's/"started": "[^"]*"/"started": "TIME"/' \
		    -e 's/"ended": "[^"]*"/"ended": "TIME"/' \
		    "$work/RUN/result.json" >"$work/RUN/result.json.new" &&
			mv "$work/RUN/result.json.new" "$work/RUN/result.json"
	fi
fi

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
: >"$work.files"
[ -d "$case_dir/expected" ] && (cd "$case_dir/expected" && find . -type f ! -name stdout ! -name stderr ! -name exit) >"$work.files"
while IFS= read -r f; do
	check "$case_dir/expected/$f" "$work/$f" "$f" || true
done <"$work.files"
if [ -f "$case_dir/check.sh" ]; then
	(cd "$work" && QWE_STDOUT="$work.stdout" sh "$case_dir/check.sh") || { echo "check.sh failed" >&2; fail=1; }
fi
rm -f "$work.stdout" "$work.stderr" "$work.files"
[ "$fail" = 0 ]
