-- Every built-in plugin passes the checks a project plugin passes in
-- `qwe validate`: strict metaschema, contract and luacheck. Arguments are the
-- plugin source files (.../<name>/plugin.lua and .../<name>/schema.json). Lua
-- next to no schema.json (an execution backend) gets luacheck only.
local plugincheck = require("qwe.plugincheck")

local function read(path)
  local f = assert(io.open(path, "rb"))
  local text = f:read("*a")
  f:close()
  return text
end

local function exists(path)
  local f = io.open(path, "rb")
  if f then f:close() end
  return f ~= nil
end

local failed, checked = 0, 0
for _, path in ipairs({ ... }) do
  if path:match("%.lua$") then
    local dir = path:match("^(.*)/[^/]*$")
    local name = dir:match("([^/]*)$")
    local schema = dir .. "/schema.json"
    local problems
    if exists(schema) then
      problems = plugincheck.check_builtin(name, path, read(path), schema, read(schema))
    else
      problems = plugincheck.check_lua_only(name, path, read(path))
    end
    checked = checked + 1
    for _, p in ipairs(problems) do
      failed = failed + 1
      print(p.where .. ": " .. p.message)
    end
  end
end
if checked == 0 then error("no plugin sources given", 0) end
if failed > 0 then error(failed .. " problem(s) in built-in plugins", 0) end
print("ok   " .. checked .. " built-in plugin source(s)")
