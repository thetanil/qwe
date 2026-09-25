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

# A source over the size cap (1 MiB, about 30 times the largest module) is refused
# before anything is allocated for it.
head -c 1048577 /dev/zero | tr "\\0" "-" >"$dir/big.lua"
"$bcembed" "$dir/out.c" "big=$dir/big.lua" 2>"$dir/err"
check "over the cap: exit" 1 $?
expect_in "over the cap: message" "$dir/err" "cannot read $dir/big.lua: File too large"

# No output file, or an argument that is not <module>=<file>: usage errors.
"$bcembed" 2>"$dir/err"
check "no arguments: exit" 2 $?
expect_in "no arguments: message" "$dir/err" "usage: bcembed"
"$bcembed" "$dir/out.c" "m" 2>"$dir/err"
check "bad argument: exit" 2 $?
expect_in "bad argument: message" "$dir/err" "bad argument m"

# A source that is missing, or that cannot be sized (a pipe), is not read.
"$bcembed" "$dir/out.c" "m=$dir/missing.lua" 2>"$dir/err"
check "missing source: exit" 1 $?
expect_in "missing source: message" "$dir/err" "cannot read $dir/missing.lua"
echo "return 1" | "$bcembed" "$dir/out.c" "m=/dev/stdin" 2>"$dir/err"
check "pipe: exit" 1 $?
expect_in "pipe: message" "$dir/err" "cannot read /dev/stdin"

# A .json file is embedded as a module that returns its text.
echo '{"a": 1}' >"$dir/d.json"
"$bcembed" "$dir/out.c" "d=$dir/d.json" "m=$dir/m.lua" 2>"$dir/err"
check "json: exit" 0 $?
expect_in "json: table" "$dir/out.c" '{"d", '

# The chunk name (what a Lua error names the module by) has a length limit.
long=$(head -c 300 /dev/zero | tr "\\0" "n")
"$bcembed" "$dir/out.c" "$long=$dir/m.lua" 2>"$dir/err"
check "long name: exit" 1 $?
expect_in "long name: message" "$dir/err" "module name too long"

# A source that does not compile fails the build, naming the module.
echo 'return +' >"$dir/bad.lua"
"$bcembed" "$dir/out.c" "bad=$dir/bad.lua" 2>"$dir/err"
check "syntax error: exit" 1 $?
expect_in "syntax error: message" "$dir/err" "bcembed: bad:"

exit $fail
