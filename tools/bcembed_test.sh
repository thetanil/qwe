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

# A source over the size cap (1 MiB, about 30 times the largest module) is refused: reading
# stops one byte past the cap, so a larger file is never read (or held) whole.
head -c 1048577 /dev/zero | tr "\\0" "-" >"$dir/big.lua"
"$bcembed" "$dir/out.c" "big=$dir/big.lua" 2>"$dir/err"
check "over the cap: exit" 1 $?
expect_in "over the cap: message" "$dir/err" "cannot read $dir/big.lua: File too large"
# Exactly the cap is accepted: 1 MiB of "-" is one long Lua comment.
head -c 1048576 /dev/zero | tr "\\0" "-" >"$dir/cap.lua"
"$bcembed" "$dir/out.c" "cap=$dir/cap.lua" 2>"$dir/err"
check "at the cap: exit" 0 $?

# No output file, or an argument that is not <module>=<file>: usage errors.
"$bcembed" 2>"$dir/err"
check "no arguments: exit" 2 $?
expect_in "no arguments: message" "$dir/err" "usage: bcembed"
"$bcembed" "$dir/out.c" "m" 2>"$dir/err"
check "bad argument: exit" 2 $?
expect_in "bad argument: message" "$dir/err" "bad argument m"

# A missing source is not read. One that opens but cannot be read (a directory) fails with
# the read's own error.
"$bcembed" "$dir/out.c" "m=$dir/missing.lua" 2>"$dir/err"
check "missing source: exit" 1 $?
expect_in "missing source: message" "$dir/err" "cannot read $dir/missing.lua"
mkdir "$dir/adir.lua"
"$bcembed" "$dir/out.c" "m=$dir/adir.lua" 2>"$dir/err"
check "directory: exit" 1 $?
expect_in "directory: message" "$dir/err" "cannot read $dir/adir.lua: Is a directory"

# A source is read to its end, not sized first (CERT FIO19-C, FIO45-C), so a stream with no
# size, a pipe, is read like a file.
echo "return 1" | "$bcembed" "$dir/out.c" "m=/dev/stdin" 2>"$dir/err"
check "pipe: exit" 0 $?
expect_in "pipe: table" "$dir/out.c" '{"m", '

# A .json file is embedded as a module that returns its text.
echo '{"a": 1}' >"$dir/d.json"
"$bcembed" "$dir/out.c" "d=$dir/d.json" "m=$dir/m.lua" 2>"$dir/err"
check "json: exit" 0 $?
expect_in "json: table" "$dir/out.c" '{"d", '

# Every byte of a .json file is embedded, a NUL included: the text after it (the ZZZ marker,
# bytes 90,90,90) must reach the bytecode. Formatting it with %s used to stop at the NUL.
printf '{"a": "x\000ZZZ"}\n' >"$dir/nul.json"
"$bcembed" "$dir/out.c" "n=$dir/nul.json" 2>"$dir/err"
check "json with a NUL: exit" 0 $?
tr -d '\n' <"$dir/out.c" >"$dir/flat.c"
expect_in "json with a NUL: text after it kept" "$dir/flat.c" "90,90,90,"

# The chunk name (what a Lua error names the module by) has a length limit.
long=$(head -c 300 /dev/zero | tr "\\0" "n")
"$bcembed" "$dir/out.c" "$long=$dir/m.lua" 2>"$dir/err"
check "long name: exit" 1 $?
expect_in "long name: message" "$dir/err" "module name too long"
"$bcembed" "$dir/out.c" "$long=$dir/d.json" 2>"$dir/err"
check "long name, json: exit" 1 $?
expect_in "long name, json: message" "$dir/err" "module name too long"

# A source that does not compile fails the build, naming the module.
echo 'return +' >"$dir/bad.lua"
"$bcembed" "$dir/out.c" "bad=$dir/bad.lua" 2>"$dir/err"
check "syntax error: exit" 1 $?
expect_in "syntax error: message" "$dir/err" "bcembed: bad:"

# A .json text that contains the wrapper's closing bracket ends the long string early, so it
# does not load either: the build fails naming the module rather than embedding a cut string.
printf '{"a": "]=====]"}\n' >"$dir/close.json"
"$bcembed" "$dir/out.c" "close=$dir/close.json" 2>"$dir/err"
check "json closes the wrapper: exit" 1 $?
expect_in "json closes the wrapper: message" "$dir/err" "bcembed: close:"

exit $fail
