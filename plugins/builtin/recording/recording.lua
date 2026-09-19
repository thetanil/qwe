-- The recording execution backend, for tests. It runs nothing: it records
-- every command it is asked to run, with its stdin, and returns scripted
-- results in order. A command that was not scripted is an error, so a plugin
-- test fails on it.
--
-- A script entry is { expect = <exact command> | match = <Lua pattern>,
-- code = 0, stdout = "", stderr = "", stdin = <exact stdin, if it is checked> }.
local M = {}

local Backend = {}
Backend.__index = Backend

-- Returns a backend that answers with the entries of script, first to last.
function M.new(script)
  return setmetatable({ script = script or {}, next_entry = 1, calls = {} }, Backend)
end

local function unexpected(self, why, command, stdin)
  error(string.format("unexpected command #%d: %s\n  command: %s\n  stdin: %s",
    #self.calls, why, command, stdin == nil and "(none)" or string.format("%q", stdin)), 0)
end

-- Records the command and returns the next scripted result.
function Backend:run(command, stdin)
  self.calls[#self.calls + 1] = { command = command, stdin = stdin }
  local entry = self.script[self.next_entry]
  if entry == nil then unexpected(self, "the script has no more entries", command, stdin) end
  if entry.expect ~= nil and entry.expect ~= command then
    unexpected(self, "expected " .. entry.expect, command, stdin)
  end
  if entry.match ~= nil and not command:find(entry.match) then
    unexpected(self, "expected a match for " .. entry.match, command, stdin)
  end
  if entry.stdin ~= nil and entry.stdin ~= stdin then
    unexpected(self, "expected stdin " .. string.format("%q", entry.stdin), command, stdin)
  end
  self.next_entry = self.next_entry + 1
  return { code = entry.code or 0, stdout = entry.stdout or "", stderr = entry.stderr or "" }
end

-- Fails if scripted results were left unused.
function Backend:done()
  if self.next_entry <= #self.script then
    error(string.format("%d scripted result(s) were never used", #self.script - self.next_entry + 1), 0)
  end
end

return M
