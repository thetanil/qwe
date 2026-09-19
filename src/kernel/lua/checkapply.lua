-- The check/apply protocol (design §12.3). A step plugin exports
--   check(with, ctx) -> needs_change, outputs
--   apply(with, ctx)
-- where check never changes anything, needs_change is true when the target is
-- not yet in the desired state, and outputs (optional) are the step outputs
-- as the target is now. ctx.backend is the job's execution backend.
--
-- run() calls check, calls apply only if a change is needed, then calls check
-- again. `changed` is true only if apply ran. If the second check still needs
-- a change, the step fails with reason not-converged.
local M = {}

-- Returns the step's result: { status = "ok" | "failed", changed, outputs, reason }.
function M.run(mod, with, ctx)
  local needs, outputs = mod.check(with, ctx)
  if not needs then
    return { status = "ok", changed = false, outputs = outputs or {} }
  end
  mod.apply(with, ctx)
  needs, outputs = mod.check(with, ctx)
  if needs then
    return { status = "failed", reason = "not-converged", changed = true, outputs = {} }
  end
  return { status = "ok", changed = true, outputs = outputs or {} }
end

return M
