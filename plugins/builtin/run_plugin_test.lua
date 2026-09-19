-- Runs a plugin's test file inside qwe's Lua state: run_plugin_test.lua <test.lua>.
-- The test file receives `case` and `eq` as its arguments:
--   local case, eq = ...
--   case("name", function() ... end)
-- Every case runs, and one that raises an error fails. Exit status is 1 if any failed.
local path = ...
local cases = {}

local function case(name, fn)
  cases[#cases + 1] = { name = name, fn = fn }
end

local function eq(want, got, what)
  if want ~= got then
    error(string.format("%s: want %q, got %q", what or "value", tostring(want), tostring(got)), 2)
  end
end

local chunk = assert(loadfile(path))
chunk(case, eq)
if #cases == 0 then error(path .. ": no cases", 0) end
local failed = 0
for _, c in ipairs(cases) do
  local ok, err = pcall(c.fn)
  if ok then
    print("ok   " .. c.name)
  else
    failed = failed + 1
    print("FAIL " .. c.name .. ": " .. tostring(err))
  end
end
if failed > 0 then error(failed .. " of " .. #cases .. " case(s) failed", 0) end
