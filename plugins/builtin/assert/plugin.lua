-- The built-in assert step plugin: a workflow checks its own results. It runs no command
-- on the target; check does the comparison and the step is always unchanged. A mismatch
-- raises a plugin error, which the kernel's usual secret redaction applies to like any
-- other step output.
local M = {}

-- Long values are cut at 200 characters, so a mismatch on a large file's content does
-- not flood the log.
local function cut(s)
  if #s <= 200 then return s end
  return s:sub(1, 200) .. "..."
end

-- The schema's oneOf guarantees exactly one of these is set.
local function op_and_expected(with)
  if with.equals ~= nil then return "equals", with.equals end
  if with.contains ~= nil then return "contains", with.contains end
  return "matches", with.matches
end

local function matched(op, expected, actual)
  if op == "equals" then return actual == expected end
  if op == "contains" then return actual:find(expected, 1, true) ~= nil end
  return actual:match(expected) ~= nil
end

function M.check(with)
  local op, expected = op_and_expected(with)
  if matched(op, expected, with.actual) then return false end
  error("assert: " .. (with.message or "values differ") .. ": expected " .. op .. " "
    .. cut(expected) .. ", actual " .. cut(with.actual), 0)
end

function M.apply() end

return M
