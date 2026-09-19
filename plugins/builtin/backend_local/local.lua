-- The `local` execution backend: commands run on the operator host itself.
local exec = require("qwe.exec")

local M = {}

-- Translates a shell script into the argv that runs it on the target.
function M.argv(script)
  return { "sh", "-c", script }
end

local Backend = {}
Backend.__index = Backend

-- Runs a shell script, with stdin (a string, or nil for none) as its input.
-- Returns { code, stdout, stderr }; code is -N if the script died of signal N.
function Backend.run(_self, script, stdin)
  local code, out, err = exec.run(M.argv(script), stdin)
  if code == nil then error("cannot run a command: " .. tostring(out), 0) end
  return { code = code, stdout = out, stderr = err }
end

-- The backend a job on the operator host uses.
function M.new()
  return setmetatable({}, Backend)
end

return M
