-- The top level of a project plugin runs when the workflow is validated: qwe
-- loads the module to check its contract. Its check and apply do not.
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
