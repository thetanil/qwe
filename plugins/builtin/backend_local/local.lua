-- The `local` execution backend: commands run on the operator host itself.
local M = {}

-- Translates a shell script into the argv that runs it on the target.
function M.argv(script)
  return { "sh", "-c", script }
end

return M
