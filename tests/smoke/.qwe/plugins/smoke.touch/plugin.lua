-- smoke_project_plugin.yml's project plugin (12): proves qwe loads and runs a plugin
-- from next to the workflow, not just the built-in ones. Idempotent: creates an empty
-- file if it is missing, and is a no-op if it already exists.
local M = {}

local function quote(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

function M.check(with, ctx)
  local stat = ctx.backend:run("stat -c %a -- " .. quote(with.path))
  return stat.code ~= 0
end

function M.apply(with, ctx)
  local touch = ctx.backend:run("touch -- " .. quote(with.path))
  if touch.code ~= 0 then
    error("smoke.touch: cannot create " .. with.path .. ": " .. touch.stderr:gsub("%s+$", ""), 0)
  end
end

return M
