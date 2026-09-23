local case, eq = ...
local recording = require("backend.recording")
local checkapply = require("qwe.checkapply")
local plugin = require("qwe.plugins").builtin_module("file.read")

local PATH = "/etc/motd"
local STAT = "stat -c '%a %U' -- '" .. PATH .. "'"
local CAT = "cat -- '" .. PATH .. "'"
local SHA = "sha256sum -- '" .. PATH .. "'"
local HASH = string.rep("ab", 32)

local function read(script, with)
  local rec = recording.new(script)
  local result = checkapply.run(plugin, with, { backend = rec })
  rec:done()
  return result, rec
end

case("outputs", function()
  local result = read({
    { expect = STAT, stdout = "644 root\n" },
    { expect = CAT, stdout = "hello\n" },
    { expect = SHA, stdout = HASH .. "  " .. PATH .. "\n" },
  }, { path = PATH })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed: check never has work for apply")
  eq(true, result.outputs.exists, "exists")
  eq("hello\n", result.outputs.content, "content, trailing newline kept")
  eq(HASH, result.outputs.sha256, "sha256")
  eq("0644", result.outputs.mode, "mode")
  eq("root", result.outputs.owner, "owner")
end)

case("missing", function()
  local result, rec = read({
    { expect = STAT, code = 1 },
  }, { path = PATH })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
  eq(false, result.outputs.exists, "exists")
  eq("", result.outputs.content, "content")
  eq("", result.outputs.sha256, "sha256")
  eq("", result.outputs.mode, "mode")
  eq("", result.outputs.owner, "owner")
  eq(1, #rec.calls, "a missing file stops after stat: no cat, no sha256sum")
end)

case("denied", function()
  local function fails(script)
    local rec = recording.new(script)
    local ok, msg = pcall(checkapply.run, plugin, { path = PATH }, { backend = rec })
    eq(false, ok, "pcall ok")
    rec:done()
    return msg
  end

  eq("file.read: cannot read " .. PATH .. ": Permission denied",
    fails({
      { expect = STAT, stdout = "644 root\n" },
      { expect = CAT, code = 1, stderr = "cat: " .. PATH .. ": Permission denied\n" },
    }), "a cat failure")

  eq("file.read: cannot read " .. PATH .. ": Permission denied",
    fails({
      { expect = STAT, stdout = "644 root\n" },
      { expect = CAT, stdout = "hello\n" },
      { expect = SHA, code = 1, stderr = "sha256sum: " .. PATH .. ": Permission denied\n" },
    }), "a sha256sum failure")

  eq("file.read: cannot read " .. PATH .. ": no colon here",
    fails({
      { expect = STAT, stdout = "644 root\n" },
      { expect = CAT, code = 1, stderr = "no colon here\n" },
    }), "stderr with no ': ' falls back to the whole trimmed text")
end)

case("quotes_the_path", function()
  local path = "/tmp/it's"
  local stat = "stat -c '%a %U' -- '/tmp/it'\\''s'"
  local rec = recording.new({ { expect = stat, code = 1 } })
  local result = checkapply.run(plugin, { path = path }, { backend = rec })
  eq(false, result.outputs.exists, "exists")
  rec:done()
end)
