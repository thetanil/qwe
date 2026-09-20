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

# qwe.fs reports what it cannot read or create as nil plus a message, never a raise.
cat >"$dir/fs.lua" <<'LUA'
local fs = require("qwe.fs")
local root = ...
local function fails(what, r, msg)
	assert(r == nil and type(msg) == "string" and #msg > 0, what .. ": want nil, message")
	return msg
end
fails("list missing", fs.list(root .. "/nope"))
fails("list a file", fs.list(root .. "/plain"))
fails("private_dir under a file", fs.private_dir(root .. "/plain/x"))
fails("private_dir on a file", fs.private_dir(root .. "/plain"))
assert(fs.isdir(root .. "/plain") == false and fs.isdir(root .. "/nope") == false)
LUA
: >"$dir/plain"
"$luarun" "$dir/fs.lua" "$dir" 2>"$dir/err"
check "fs errors" 0 $?
# A directory it cannot read (root reads anything, so this needs a user).
if [ "$(id -u)" != 0 ]; then
	mkdir "$dir/locked" && chmod 000 "$dir/locked"
	echo 'local r, m = require("qwe.fs").list(...); assert(r == nil and m:find("Permission denied"), tostring(m))' \
		>"$dir/locked.lua"
	"$luarun" "$dir/locked.lua" "$dir/locked" 2>"$dir/err"
	check "unreadable directory" 0 $?
	chmod 700 "$dir/locked"
fi

# qwe.secrets.reveal: a table with no value, no key file, and a value that is not an envelope.
cat >"$dir/sec.lua" <<'LUA'
local s = require("qwe.secrets")
local ok, e = pcall(s.reveal, {})
assert(not ok and tostring(e):find("not a secret"), "no value: " .. tostring(e))
ok, e = pcall(s.reveal, { value = "qwe:1:xchacha20poly1305:AAAA" })
assert(not ok and tostring(e):find("cannot read the key file"), "no key: " .. tostring(e))
LUA
mkdir "$dir/nokey"
HOME="$dir/nokey" "$luarun" "$dir/sec.lua" 2>"$dir/err"
check "reveal without a key" 0 $?
cat >"$dir/sec2.lua" <<'LUA'
local s = require("qwe.secrets")
for _, bad in ipairs({ "plain text", "qwe:1:xchacha20poly1305:", "qwe:1:xchacha20poly1305:!!!!", "qwe:1:xchacha20poly1305:AAAA" }) do
	local ok, e = pcall(s.reveal, { value = bad })
	assert(not ok and tostring(e):find("cannot decrypt a secret"), bad .. ": " .. tostring(e))
end
LUA
mkdir -p "$dir/withkey/.config/qwe"
head -c 32 /dev/zero >"$dir/withkey/.config/qwe/secret"
chmod 600 "$dir/withkey/.config/qwe/secret"
HOME="$dir/withkey" "$luarun" "$dir/sec2.lua" 2>"$dir/err"
check "reveal a malformed envelope" 0 $?

# qwe.exec.run's poll loop: a command that never reads its stdin (SIGPIPE is
# ignored, the write just fails), stdin larger than the pipe (partial writes),
# a command that cannot be exec'd, a timeout, a command killed by a signal.
cat >"$dir/exec.lua" <<'LUA'
local exec = require("qwe.exec")
local big = string.rep("x", 1 << 20)
local code = exec.run({ "true" }, big)
assert(code == 0, "no read: " .. tostring(code))
local out
code, out = exec.run({ "cat" }, big)
assert(code == 0 and #out == #big, "partial writes: " .. tostring(code) .. " " .. #out)
code = exec.run({ "/nonexistent/cmd" })
assert(code == 127, "missing command: " .. tostring(code))
local r, why = exec.run({ "sleep", "5" }, nil, { timeout = 0.2 })
assert(r == nil and why:find("timed out"), "timeout: " .. tostring(why))
code = exec.run({ "sh", "-c", "kill -9 $$" })
assert(code == -9, "signalled: " .. tostring(code))
LUA
"$luarun" "$dir/exec.lua" 2>"$dir/err"
check "exec poll loop" 0 $?

exit $fail
