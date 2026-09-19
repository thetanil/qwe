local M = {}

function M.check(_with)
  return true
end

function M.apply(_with)
  print(misspelled) -- luacheck: ignore 113
end

return M
