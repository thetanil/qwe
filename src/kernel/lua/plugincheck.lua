-- The three checks every plugin passes, built-in (as Bazel tests) and project
-- (in `qwe validate` and `qwe run`): its schemas against the strict
-- metaschema, its contract, and luacheck.
local cbor = require("qwe.cbor")
local plugins = require("qwe.plugins")
local strict = require("qwe.strict")
local validate = require("qwe.validate")

local M = {}

local function unesc(token)
  return (token:gsub("~1", "/"):gsub("~0", "~"))
end

-- Returns a list of { pointer, message }: how a schema (a decoded table)
-- breaks the qwe strict metaschema. A typo'd keyword is reported at its key.
function M.metaschema_errors(doc)
  local list = {}
  for _, err in ipairs(validate.check(plugins.decode_schema(require("strict_metaschema")), doc)) do
    if err.kind == "key" then
      err.message = 'unknown keyword "' .. unesc(err.pointer:match("([^/]*)$")) .. '"'
    end
    list[#list + 1] = err
  end
  table.sort(list, function(a, b) return a.pointer < b.pointer end)
  return list
end

local function line_col(text, pos)
  local line, col = 1, 1
  for i = 1, pos - 1 do
    if text:sub(i, i) == "\n" then
      line, col = line + 1, 1
    else
      col = col + 1
    end
  end
  return line, col
end

-- A schema.json is { "kind": "check-apply" | "argv", "with": <schema>,
-- "outputs": { name: <schema with "secret": bool> } }. The kind says which
-- functions the plugin exports, so that it can be checked without running it.
local function check_schema_file(path, text, problems)
  local function add(message) problems[#problems + 1] = { where = path, message = message } end
  local doc, pos, err = require("dkjson").decode(text, 1, cbor.null, cbor.map_mt, cbor.array_mt)
  if doc == nil then
    local line, col = line_col(text, pos or 1)
    problems[#problems + 1] = { where = path .. ":" .. line .. ":" .. col, message = "invalid JSON: " .. tostring(err) }
    return nil
  end
  if getmetatable(doc) ~= cbor.map_mt then
    add("must be an object with a with: schema and optional outputs")
    return nil
  end
  local ok = true
  for key in pairs(doc) do
    if key ~= "kind" and key ~= "with" and key ~= "outputs" then
      add('at /' .. key .. ': unknown key "' .. key .. '" (only kind, with and outputs)')
      ok = false
    end
  end
  if doc.kind ~= "check-apply" and doc.kind ~= "argv" then
    add('at /kind: required, "check-apply" (the plugin exports check and apply) or "argv" (a run-like plugin, which exports argv)')
    ok = false
  end
  if type(doc.with) ~= "table" or getmetatable(doc.with) ~= cbor.map_mt then
    add("at /with: a with: schema (a JSON object) is required")
    return nil
  end
  local function meta(prefix, schema_doc)
    for _, e in ipairs(M.metaschema_errors(schema_doc)) do
      add("at " .. prefix .. e.pointer .. ": " .. e.message)
      ok = false
    end
  end
  meta("/with", doc.with)
  local outputs = doc.outputs
  if outputs ~= nil then
    if getmetatable(outputs) ~= cbor.map_mt then
      add("at /outputs: must be an object mapping an output name to its schema")
      return nil
    end
    local names = {}
    for name in pairs(outputs) do names[#names + 1] = name end
    table.sort(names)
    for _, name in ipairs(names) do
      local out = outputs[name]
      meta("/outputs/" .. name, out)
      if type(out) == "table" and type(out.secret) ~= "boolean" then
        add('at /outputs/' .. name .. ': output "' .. name .. '" must declare "secret": true or false')
        ok = false
      end
    end
  end
  if not ok then return nil end
  return doc
end

-- Reads plugin.lua: luacheck, then compile. Compiling runs nothing. With run set (built-in
-- plugins only, which are our own code) the chunk is also executed, for its module.
local function check_lua(name, path, src, problems, run)
  local luacheck = require("luacheck")
  local report = luacheck.check_strings({ src }, { std = "luajit", max_line_length = false })
  local file = report[1]
  local flagged = false
  if file.fatal then
    problems[#problems + 1] = { where = path, message = "luacheck: " .. tostring(file.fatal) .. ": " .. tostring(file.msg) }
    flagged = true
  else
    for _, event in ipairs(file) do
      problems[#problems + 1] = {
        where = path .. ":" .. event.line .. ":" .. event.column,
        message = "luacheck: (" .. (event.code:sub(1, 1) == "0" and "E" or "W") .. event.code .. ") "
          .. luacheck.get_message(event),
      }
      flagged = true
    end
  end
  if flagged then return nil end
  local chunk, err = loadstring(src, "@" .. path)
  if not chunk then
    problems[#problems + 1] = { where = path, message = "cannot load: " .. tostring(err) }
    return nil
  end
  if not run then return true end
  local ok, mod = pcall(strict.run, name, chunk)
  if not ok then
    problems[#problems + 1] = { where = path, message = "cannot load: " .. tostring(mod) }
    return nil
  end
  return mod
end

-- The contract for a built-in plugin, whose module is loaded to check it.
local function check_contract(path, kind, mod, problems)
  local why = plugins.contract_problem(kind, mod)
  if why then problems[#problems + 1] = { where = path, message = why } end
end

-- Checks one project plugin without running any of it: schema.json (with its kind),
-- luacheck, and that plugin.lua compiles. `lua_src` and `schema_text` are the file
-- contents; the paths are only for messages. Returns a list of { where, message },
-- empty when the plugin passes, and the decoded schema.json when it parsed. Whether
-- the module really exports what its kind says is checked when a step uses it
-- (plugins.run_step): that takes running it, which validation never does.
function M.check(name, lua_path, lua_src, schema_path, schema_text)
  local problems = {}
  local schema_doc = check_schema_file(schema_path, schema_text, problems)
  check_lua(name, lua_path, lua_src, problems)
  return problems, schema_doc
end

-- The same for a built-in plugin, and it also loads the module to check its contract.
function M.check_builtin(name, lua_path, lua_src, schema_path, schema_text)
  local problems = {}
  local schema_doc = check_schema_file(schema_path, schema_text, problems)
  local mod = check_lua(name, lua_path, lua_src, problems, true)
  if mod ~= nil and schema_doc then check_contract(lua_path, schema_doc.kind, mod, problems) end
  return problems, schema_doc
end

-- luacheck only, for built-in Lua that is not a step plugin (a backend).
function M.check_lua_only(name, lua_path, lua_src)
  local problems = {}
  check_lua(name, lua_path, lua_src, problems, true)
  return problems
end

return M
