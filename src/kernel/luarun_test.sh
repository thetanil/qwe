#!/bin/sh
# usage: luarun_test.sh <luarun>
luarun=$1
[ -x "$luarun" ] || { echo "no luarun binary: $luarun" >&2; exit 3; }
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

# The script's arguments arrive as its `...`, in order.
cat >"$dir/args.lua" <<'LUA'
print(select("#", ...), ...)
LUA
out=$("$luarun" "$dir/args.lua" one "two words" 3)
check "arguments: exit" 0 $?
[ "$out" = "3	one	two words	3" ] || { echo "FAIL arguments: got '$out'" >&2; fail=1; }

# The built-in modules can be required.
cat >"$dir/builtin.lua" <<'LUA'
local cbor = require("qwe.cbor")
assert(cbor.decode("\x80"))
require("schema")
LUA
"$luarun" "$dir/builtin.lua" 2>"$dir/err"
check "built-in modules" 0 $?

# A runtime error: message on stderr, exit 1.
echo 'error("boom", 0)' >"$dir/raises.lua"
"$luarun" "$dir/raises.lua" 2>"$dir/err"
check "runtime error: exit" 1 $?
expect_in "runtime error: message" "$dir/err" "boom"

# A failed assert is a failure too.
echo 'assert(false, "nope")' >"$dir/assert.lua"
"$luarun" "$dir/assert.lua" 2>"$dir/err"
check "failed assert: exit" 1 $?

# A syntax error: exit 2, before anything runs.
echo 'this is not lua' >"$dir/syntax.lua"
"$luarun" "$dir/syntax.lua" 2>"$dir/err"
check "syntax error: exit" 2 $?
expect_in "syntax error: message" "$dir/err" "syntax.lua"

# A script that does not exist: exit 2.
"$luarun" "$dir/missing.lua" 2>"$dir/err"
check "missing script: exit" 2 $?
expect_in "missing script: message" "$dir/err" "missing.lua"

# No script at all: usage, exit 2.
"$luarun" 2>"$dir/err"
check "no arguments: exit" 2 $?
expect_in "no arguments: usage" "$dir/err" "usage"

# os.exit from the script sets the exit status.
echo 'os.exit(7)' >"$dir/exit.lua"
"$luarun" "$dir/exit.lua"
check "os.exit" 7 $?

exit $fail
