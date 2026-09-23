#!/bin/sh
# usage: bcembed_test.sh <bcembed>
bcembed=$1
[ -x "$bcembed" ] || { echo "no bcembed binary: $bcembed" >&2; exit 3; }
dir=$(mktemp -d) || exit 3
trap 'rm -rf "$dir"' EXIT

fail=0
check() { # <what> <want-exit> <got-exit>
	if [ "$2" != "$3" ]; then
		echo "FAIL $1: want exit $2, got $3" >&2
		fail=1
	fi
}
expect_in() { # <what> <file> <text>
	grep -q -- "$3" "$2" || { echo "FAIL $1: '$3' not in: $(cat "$2")" >&2; fail=1; }
}

echo 'return 1' >"$dir/m.lua"

# The happy path writes a table naming the module.
"$bcembed" "$dir/out.c" "m=$dir/m.lua" 2>"$dir/err"
check "writes: exit" 0 $?
expect_in "writes: table" "$dir/out.c" '{"m", '

# A write that fails (here at fclose, when the buffer is flushed to a full
# device) fails the build instead of leaving a short table that still compiles.
"$bcembed" /dev/full "m=$dir/m.lua" 2>"$dir/err"
check "full device: exit" 1 $?
expect_in "full device: message" "$dir/err" "cannot write /dev/full"

exit $fail
