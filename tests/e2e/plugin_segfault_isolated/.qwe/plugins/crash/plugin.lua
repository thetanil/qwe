local ffi = require("ffi")

local M = {}

function M.check(_with)
  return true
end

-- Calls a function at address 0.
function M.apply(_with)
  ffi.cast("void (*)(void)", 0)()
end

return M
