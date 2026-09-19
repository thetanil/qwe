-- `become:` (ticket 14): running a step's commands as another user through
-- sudo. Both execution backends prepend the same words, so that sudo wraps the
-- shell that reads the env preamble: the env is read after sudo's env_reset,
-- inside the new user's process, and survives it.
local M = {}

-- The words to put before the command for a step's become: value, as a list
-- (empty for no become): true is root, a string a user name, an integer a uid.
-- 0 is uid 0, which is not the same as false.
function M.prefix(value)
  if value == nil or value == false then return {} end
  if value == true then return { "sudo", "-n" } end
  if type(value) == "string" then return { "sudo", "-n", "-u", value } end
  if type(value) == "number" then return { "sudo", "-n", "-u", string.format("#%d", value) } end
  error("become: must be true, a user name or a uid", 0)
end

-- Runs a trivial command through the backend to learn whether sudo will let the
-- step through. sudo -n never asks for a password, so a refusal is immediate.
-- Returns nil if the step may run, or sudo's own message. ssh's own failure
-- (exit 255) is not a refusal: the step fails as connection-lost when it runs.
function M.check(backend)
  if #M.prefix(backend.become) == 0 then return nil end
  local r = backend:run("true")
  if r.code == 0 or (backend.remote and r.code == 255) then return nil end
  local why = r.stderr:gsub("%s+$", "")
  return why ~= "" and why or ("sudo exited with " .. r.code)
end

return M
