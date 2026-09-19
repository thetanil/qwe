-- The `local` execution backend: commands run on the operator host itself.
-- Every command gets the step's declared env through the stdin preamble, the
-- same path the ssh backend uses.
local exec = require("qwe.exec")

local M = {}

local Backend = {}
Backend.__index = Backend

-- Translates a shell script into what starts it on the target: the argv, and
-- the stdin (the env preamble, then the command's own stdin, unchanged).
function Backend:command(script, stdin)
  return { "sh", "-c", exec.bootstrap, "sh", script }, exec.preamble(self.env) .. (stdin or "")
end

-- Runs a shell script, with stdin (a string, or nil for none) as its input.
-- Returns { code, stdout, stderr }; code is -N if the script died of signal N.
function Backend:run(script, stdin)
  local argv, input = self:command(script, stdin)
  local code, out, err = exec.run(argv, input)
  if code == nil then error("cannot run a command: " .. tostring(out), 0) end
  return { code = code, stdout = out, stderr = err }
end

-- The backend a job on the operator host uses. opts.env is the step's declared
-- env, a { NAME = "value" } table.
function M.new(opts)
  return setmetatable({ env = opts and opts.env or {} }, Backend)
end

return M
