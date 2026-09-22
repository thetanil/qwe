local M = {}

function M.check(_with)
  return true
end

-- A genuinely missing module: qwe.plugins.run_step's own pcall must turn this
-- into a clean plugin-error, not a crash (ticket 18).
function M.apply(_with)
  require("definitely_not_a_real_module")
end

return M
