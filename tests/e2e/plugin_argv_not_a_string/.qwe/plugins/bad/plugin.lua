local M = {}

-- A run-like plugin whose command has an argument that is not a string.
function M.argv(_with, _ctx)
  return { "echo", {} }
end

return M
