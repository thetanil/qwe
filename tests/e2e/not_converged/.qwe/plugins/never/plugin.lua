local M = {}

-- Always needs a change, and apply never makes one.
function M.check(_with)
  return true
end

function M.apply(_with)
  print("applying")
end

return M
