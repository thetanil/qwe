-- The built-in file.ensure step plugin. Ticket 10 implements it; until then a
-- step that uses it fails with reason plugin-error.
local M = {}

function M.check(_with)
  error("file.ensure: not implemented", 0)
end

function M.apply(_with)
  error("file.ensure: not implemented", 0)
end

return M
