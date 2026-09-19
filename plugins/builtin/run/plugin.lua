-- The built-in `run:` step plugin. It runs in the forked step child and
-- returns what the child execs: the argv, and the stdin that carries the
-- step's env to the command.
local M = {}

function M.argv(with, ctx)
  return ctx.backend:command(with.run)
end

return M
