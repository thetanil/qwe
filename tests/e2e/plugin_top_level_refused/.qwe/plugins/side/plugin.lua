-- A plugin has no top-level code: this would run when the file loads.
local f = assert(io.open("top-level-ran", "w"))
f:write("yes")
f:close()

local M = {}

function M.check(_with)
  return true
end

function M.apply(_with)
end

return M
