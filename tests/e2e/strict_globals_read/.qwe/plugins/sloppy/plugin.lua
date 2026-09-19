local M = {}

function M.check(_with)
  return false
end

function M.apply(_with)
  print(misspelled) -- luacheck: ignore 113
end

return M
