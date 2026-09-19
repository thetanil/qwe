local M = {}

function M.check(_with)
  return false
end

function M.apply(with)
  print("hello, " .. with.name)
end

return M
