local case, eq = ...
local recording = require("backend.recording")

case("returns_scripted_results_in_order", function()
  local rec = recording.new({
    { expect = "one", stdout = "1" },
    { expect = "two", code = 3, stderr = "bad" },
  })
  local a = rec:run("one")
  local b = rec:run("two", "input")
  eq(0, a.code, "first code")
  eq("1", a.stdout, "first stdout")
  eq(3, b.code, "second code")
  eq("bad", b.stderr, "second stderr")
  eq("input", rec.calls[2].stdin, "recorded stdin")
  rec:done()
end)

case("unexpected_command_fails", function()
  local rec = recording.new({ { expect = "one" } })
  local ok, err = pcall(rec.run, rec, "something else")
  eq(false, ok, "an unexpected command")
  assert(err:find("unexpected command", 1, true), err)
  -- Running past the end of the script is unexpected too.
  rec = recording.new({})
  ok = pcall(rec.run, rec, "anything")
  eq(false, ok, "a command with nothing scripted")
end)

case("unused_results_fail_done", function()
  local rec = recording.new({ { expect = "one" } })
  local ok = pcall(rec.done, rec)
  eq(false, ok, "done with an unused result")
end)
