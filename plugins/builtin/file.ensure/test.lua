local case, eq = ...
local recording = require("backend.recording")
local checkapply = require("qwe.checkapply")
local plugin = require("qwe.plugins").builtin_module("file.ensure")

local PATH = "/etc/motd"
local STAT = "stat -c %a -- '" .. PATH .. "'"
local CAT = "cat -- '" .. PATH .. "'"

local function ensure(script, with)
  local rec = recording.new(script)
  local result = checkapply.run(plugin, with, { backend = rec })
  rec:done()
  return result, rec
end

case("mode_only_change", function()
  local result = ensure({
    { expect = STAT, stdout = "644\n" }, -- check: the mode is wrong
    { expect = CAT, stdout = "hi" },
    { expect = STAT, stdout = "644\n" }, -- apply looks again
    { expect = CAT, stdout = "hi" },
    { expect = "chmod 0600 '" .. PATH .. "'" },
    { expect = STAT, stdout = "600\n" }, -- check again: converged
    { expect = CAT, stdout = "hi" },
  }, { path = PATH, content = "hi", mode = "0600" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
  eq("0600", result.outputs.mode, "the new mode")
end)

case("already_in_place_is_unchanged", function()
  local result = ensure({
    { expect = STAT, stdout = "600\n" },
    { expect = CAT, stdout = "hi" },
  }, { path = PATH, content = "hi", mode = "600" })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
end)

case("creates_a_missing_file", function()
  local result = ensure({
    { expect = STAT, code = 1 },
    { expect = STAT, code = 1 },
    { expect = "cat > '" .. PATH .. "'", stdin = "hi" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "hi" },
  }, { path = PATH, content = "hi" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("content_via_stdin", function()
  local secret = "s3cr3t' $(rm -rf /) \n"
  local _, rec = ensure({
    { expect = STAT, code = 1 },
    { expect = STAT, code = 1 },
    { match = "^cat > ", stdin = secret },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = secret },
  }, { path = PATH, content = secret })
  for _, call in ipairs(rec.calls) do
    eq(nil, call.command:find("s3cr3t", 1, true), "content in a command line")
  end
  eq(secret, rec.calls[3].stdin, "the write's stdin")
end)

case("quotes_the_path", function()
  local stat = "stat -c %a -- '/tmp/it'\\''s'"
  local rec = recording.new({ { expect = stat, code = 1 }, { expect = stat, code = 1 } })
  local mod = { check = plugin.check, apply = function() end }
  eq("not-converged", checkapply.run(mod, { path = "/tmp/it's" }, { backend = rec }).reason, "reason")
  rec:done()
end)
