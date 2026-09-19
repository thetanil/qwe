local M = {}

function M.check(_with)
  return true
end

-- A tight loop that never yields: only the step's timeout ends it.
function M.apply(_with)
  local n = 0
  while true do
    n = n + 1
  end
end

return M
