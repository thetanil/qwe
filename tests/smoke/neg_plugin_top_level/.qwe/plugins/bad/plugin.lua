-- smoke: negative case (12). A plugin has no top-level code: this would run when the
-- file loads, before qwe knows the workflow even wants to run it.
print("top level ran")

local M = {}

function M.check(_with)
  return true
end

function M.apply(_with)
end

return M
