local M = {}

local greeted = false

-- A change is needed until apply has run.
function M.check(_with)
  return not greeted
end

function M.apply(with)
  print("hello, " .. with.name)
  greeted = true
end

return M
