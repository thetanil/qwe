local case, eq = ...
local recording = require("backend.recording")
local checkapply = require("qwe.checkapply")
local plugin = require("qwe.plugins").builtin_module("assert")

local function check(with)
  local rec = recording.new({})
  local result = checkapply.run(plugin, with, { backend = rec })
  rec:done()
  return result
end

local function fails(with)
  local rec = recording.new({})
  local ok, msg = pcall(checkapply.run, plugin, with, { backend = rec })
  eq(false, ok, "pcall ok")
  rec:done()
  return msg
end

case("equals_ops", function()
  local result = check({ actual = "hi", equals = "hi" })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed: assert never changes anything")

  eq("assert: values differ: expected equals hi, actual bye",
    fails({ actual = "bye", equals = "hi" }), "equals mismatch")
end)

case("contains_ops", function()
  eq("ok", check({ actual = "hello world", contains = "wor" }).status, "contains: substring present")

  eq("assert: values differ: expected contains xyz, actual hello world",
    fails({ actual = "hello world", contains = "xyz" }), "contains mismatch")

  -- contains is a plain substring search, not a pattern: "%d" has no special meaning here.
  eq("ok", check({ actual = "50%discount", contains = "%d" }).status, "contains treats % literally")
end)

case("matches_ops", function()
  eq("ok", check({ actual = "build-42", matches = "^build%-%d+$" }).status, "matches: a Lua pattern")

  eq("assert: values differ: expected matches ^build%-%d+$, actual build-abc",
    fails({ actual = "build-abc", matches = "^build%-%d+$" }), "matches mismatch")
end)

case("custom_message", function()
  eq("assert: the version looks wrong: expected equals 1.0, actual 2.0",
    fails({ actual = "2.0", equals = "1.0", message = "the version looks wrong" }), "message overrides the default")
end)

case("long_values_cut_at_200", function()
  local long = string.rep("x", 250)
  local msg = fails({ actual = "short", equals = long })
  local want_expected = string.rep("x", 200) .. "..."
  eq("assert: values differ: expected equals " .. want_expected .. ", actual short", msg, "expected cut at 200")

  local msg2 = fails({ actual = long, equals = "short" })
  local want_actual = string.rep("x", 200) .. "..."
  eq("assert: values differ: expected equals short, actual " .. want_actual, msg2, "actual cut at 200")

  -- exactly 200 characters is not cut.
  local exact = string.rep("y", 200)
  local msg3 = fails({ actual = "short", equals = exact })
  eq("assert: values differ: expected equals " .. exact .. ", actual short", msg3, "200 chars exactly: no cut")
end)

case("no_commands", function()
  local rec = recording.new({})
  checkapply.run(plugin, { actual = "hi", equals = "hi" }, { backend = rec })
  rec:done()
  eq(0, #rec.calls, "assert runs no command on the target")
end)
