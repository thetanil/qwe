local case, eq = ...
local recording = require("backend.recording")
local checkapply = require("qwe.checkapply")
local plugin = require("qwe.plugins").builtin_module("file.line")

local PATH = "/etc/app.conf"
local STAT = "stat -c %a -- '" .. PATH .. "'"
local CAT = "cat -- '" .. PATH .. "'"

local function line(script, with)
  local rec = recording.new(script)
  local result = checkapply.run(plugin, with, { backend = rec })
  rec:done()
  return result, rec
end

case("present_append_no_regexp", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
    { match = "^cat > ", stdin = "a\nb\nc\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\nc\n" },
  }, { path = PATH, line = "c" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("present_unchanged_when_line_already_there", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
  }, { path = PATH, line = "b" })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
end)

case("present_regexp_replaces_last_match", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEY=old\nOTHER=1\nKEY=old2\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEY=old\nOTHER=1\nKEY=old2\n" },
    { match = "^cat > ", stdin = "KEY=old\nOTHER=1\nKEY=new\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEY=old\nOTHER=1\nKEY=new\n" },
  }, { path = PATH, line = "KEY=new", regexp = "^KEY=" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("present_regexp_appends_when_no_match", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
    { match = "^cat > ", stdin = "a\nb\nKEY=new\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\nKEY=new\n" },
  }, { path = PATH, line = "KEY=new", regexp = "^KEY=" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("absent_removes_all_matches_no_regexp", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\na\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\na\n" },
    { match = "^cat > ", stdin = "b\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "b\n" },
  }, { path = PATH, line = "a", state = "absent" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("absent_regexp_removes_matches", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEEP\nDROP1\nDROP2\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEEP\nDROP1\nDROP2\n" },
    { match = "^cat > ", stdin = "KEEP\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEEP\n" },
  }, { path = PATH, line = "unused", regexp = "^DROP", state = "absent" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("absent_unchanged_when_no_match", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
  }, { path = PATH, line = "zzz", state = "absent" })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
end)

case("missing_file_present_creates", function()
  local result = line({
    { expect = STAT, code = 1 },
    { expect = STAT, code = 1 },
    { match = "^cat > ", stdin = "c\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "c\n" },
  }, { path = PATH, line = "c" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
end)

case("missing_file_absent_unchanged", function()
  local result, rec = line({
    { expect = STAT, code = 1 },
  }, { path = PATH, line = "c", state = "absent" })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
  eq(1, #rec.calls, "a missing file, absent: stops after the first stat")
end)

case("read_denied", function()
  local rec = recording.new({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, code = 1, stderr = "cat: " .. PATH .. ": Permission denied\n" },
  })
  local ok, msg = pcall(checkapply.run, plugin, { path = PATH, line = "c" }, { backend = rec })
  eq(false, ok, "pcall ok")
  eq("file.line: cannot read " .. PATH .. ": Permission denied", msg, "message")
  rec:done()
end)

case("write_denied", function()
  local rec = recording.new({
    { expect = STAT, code = 1 },
    { expect = STAT, code = 1 },
    { match = "^cat > ", code = 1, stderr = "sh: 1: cannot create " .. PATH .. ": Permission denied" },
  })
  local ok, msg = pcall(checkapply.run, plugin, { path = PATH, line = "c" }, { backend = rec })
  eq(false, ok, "pcall ok")
  eq("file.line: cannot write " .. PATH .. ": Permission denied", msg, "message")
  rec:done()
end)

case("quotes_the_path", function()
  local path = "/tmp/it's"
  local stat = "stat -c %a -- '/tmp/it'\\''s'"
  local rec = recording.new({ { expect = stat, code = 1 } })
  local result = checkapply.run(plugin, { path = path, line = "c", state = "absent" }, { backend = rec })
  eq(false, result.changed, "changed")
  rec:done()
end)

-- Edge cases: no trailing newline, a pattern matching several lines, an empty file, and a
-- line that already equals an existing one.
case("edges", function()
  local result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb" }, -- no trailing newline
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb" },
    { match = "^cat > ", stdin = "a\nb\nc\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\nc\n" },
  }, { path = PATH, line = "c" })
  eq(true, result.changed, "no trailing newline: appending still ends the file with one")

  result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEY=1\nKEY=2\nKEY=3\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEY=1\nKEY=2\nKEY=3\n" },
    { match = "^cat > ", stdin = "KEY=1\nKEY=2\nKEY=new\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "KEY=1\nKEY=2\nKEY=new\n" },
  }, { path = PATH, line = "KEY=new", regexp = "^KEY=" })
  eq(true, result.changed, "several matches: only the last line is replaced")

  result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "" },
    { match = "^cat > ", stdin = "c\n" },
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "c\n" },
  }, { path = PATH, line = "c" })
  eq(true, result.changed, "empty file: present appends")

  result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "" },
  }, { path = PATH, line = "c", state = "absent" })
  eq(false, result.changed, "empty file: absent is already in state")

  result = line({
    { expect = STAT, stdout = "644\n" },
    { expect = CAT, stdout = "a\nb\n" },
  }, { path = PATH, line = "b" })
  eq(false, result.changed, "line equals an existing line: unchanged")
end)
