-- The built-in `run:` step plugin. It runs in the forked step child and
-- returns the argv that the child execs.
local backend = require("backend.local")

local M = {}

function M.argv(with)
  return backend.argv(with.run)
end

return M
