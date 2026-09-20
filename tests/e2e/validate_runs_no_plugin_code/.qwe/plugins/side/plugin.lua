-- Validation must not run any of this. The top level runs only when a step uses the plugin.
local f = assert(io.open("top-level-ran", "w"))
f:write("yes")
f:close()

local M = {}

function M.check(_with)
  local g = assert(io.open("check-ran", "w"))
  g:close()
  return true
end

function M.apply(_with)
  local g = assert(io.open("apply-ran", "w"))
  g:close()
end

return M
