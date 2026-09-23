-- The plugin registry: built-in plugins (embedded bytecode) and project
-- plugins (source in <workflow dir>/.qwe/plugins/<name>/), each with its
-- schema.json ({ with, outputs }) and plugin.lua. All plugin code runs under
-- strict globals (qwe.strict).
local cbor = require("qwe.cbor")
local fs = require("qwe.fs")
local json = require("dkjson")
local strict = require("qwe.strict")

-- uses = false marks a plugin that is a step kind, not something `uses:` names.
local BUILTIN = {
  { name = "run", uses = false },
  { name = "file.ensure", uses = true },
  { name = "file.read", uses = true },
  { name = "file.line", uses = true },
  { name = "assert", uses = true },
}

local M = {}

local registry -- name -> plugin, of the last load()

-- Decodes a JSON schema so that objects and arrays carry qwe.cbor's metatables,
-- which is how lua-schema tells them apart.
function M.decode_schema(text)
  return assert(json.decode(text, 1, cbor.null, cbor.map_mt, cbor.array_mt))
end

-- Every built-in plugin: { name, uses, builtin, source, schema (its with:
-- schema), outputs }.
function M.builtin_all()
  local list = {}
  for _, b in ipairs(BUILTIN) do
    local doc = M.decode_schema(require("plugin_schema." .. b.name))
    list[#list + 1] = {
      name = b.name,
      uses = b.uses,
      builtin = true,
      source = "built-in",
      schema = doc.with,
      outputs = doc.outputs,
    }
  end
  return list
end

-- The built-in plugins `uses:` can name.
function M.builtin()
  local list = {}
  for _, p in ipairs(M.builtin_all()) do
    if p.uses then list[#list + 1] = p end
  end
  return list
end

local function read_file(path)
  local f, err = io.open(path, "rb")
  if not f then return nil, err end
  local text = f:read("*a")
  f:close()
  return text
end

local function join(dir, rest)
  if dir == "" then return rest end
  return dir .. "/" .. rest
end

local function is_builtin_name(name)
  for _, b in ipairs(BUILTIN) do
    if b.name == name then return true end
  end
  return false
end

-- One project plugin directory. Returns the plugin, or nil and its problems.
local function load_project(name, path)
  local problems = {}
  if is_builtin_name(name) then
    return nil, { {
      where = path,
      message = 'project plugin "' .. name .. '" has the name of the built-in plugin "' .. name
        .. '" (built-in); a project plugin cannot shadow a built-in one',
    } }
  end
  if not name:match("^[A-Za-z_][A-Za-z0-9_.%-]*$") then
    return nil, { { where = path, message = 'the plugin name "' .. name .. '" must match [A-Za-z_][A-Za-z0-9_.-]*' } }
  end
  local lua_path, schema_path = path .. "/plugin.lua", path .. "/schema.json"
  local lua_src, lua_err = read_file(lua_path)
  local schema_text, schema_err = read_file(schema_path)
  if not lua_src then problems[#problems + 1] = { where = lua_path, message = "cannot read: " .. tostring(lua_err) } end
  if not schema_text then problems[#problems + 1] = { where = schema_path, message = "cannot read: " .. tostring(schema_err) } end
  if #problems > 0 then return nil, problems end
  local found, doc = require("qwe.plugincheck").check(name, lua_path, lua_src, schema_path, schema_text)
  if #found > 0 then return nil, found end
  return {
    name = name,
    uses = true,
    project = true,
    source = path,
    schema = doc.with,
    outputs = doc.outputs,
    lua_path = lua_path,
    lua_src = lua_src,
  }
end

-- Loads the built-in plugins and the project plugins under dir/.qwe/plugins
-- (dir is the workflow's directory, "" for the current one). Returns the list
-- of plugins `uses:` can name, and a list of problems ({ where, message }) in
-- the project plugins. A project plugin with a problem is left out.
function M.load(dir)
  registry = {}
  local list, problems = {}, {}
  for _, p in ipairs(M.builtin_all()) do
    registry[p.name] = p
    if p.uses then list[#list + 1] = p end
  end
  local root = join(dir, ".qwe/plugins")
  local names = {}
  if fs.isdir(root) then
    local listed, err = fs.list(root)
    -- an unreadable directory is a problem, not a project with no plugins
    if not listed then
      problems[#problems + 1] = { where = root, message = "cannot list: " .. tostring(err) }
    else
      names = listed
    end
  end
  for _, name in ipairs(names) do
    local path = root .. "/" .. name
    if fs.isdir(path) then
      local plugin, found = load_project(name, path)
      if plugin then
        registry[name] = plugin
        list[#list + 1] = plugin
      else
        for _, p in ipairs(found) do problems[#problems + 1] = p end
      end
    end
  end
  return list, problems
end

-- The plugin's module, loaded under strict globals.
local function open(plugin)
  if plugin.mod == nil then
    local chunk
    if plugin.builtin then
      chunk = package.preload["plugin." .. plugin.name]
    else
      chunk = assert(loadstring(plugin.lua_src, "@" .. plugin.lua_path))
    end
    plugin.mod = strict.run(plugin.name, chunk)
  end
  return plugin.mod
end

local function ensure_registry()
  if registry == nil then
    registry = {}
    for _, p in ipairs(M.builtin_all()) do registry[p.name] = p end
  end
  return registry
end

-- The outputs a loaded plugin declares ({ name -> { type, secret } }), or nil.
function M.outputs_of(name)
  local p = ensure_registry()[name]
  return p and p.outputs
end

-- The module of a built-in plugin, loaded under strict globals like any
-- plugin. For plugin tests.
function M.builtin_module(name)
  local plugin = ensure_registry()[name]
  if not plugin or not plugin.builtin then error("no built-in plugin " .. name, 0) end
  return open(plugin)
end

-- Runs in the forked step child. Returns
--   argv          a run: step or a run-like plugin: the command the child execs
--   nil, result   a check/apply plugin ran here: { status = "ok" | "failed",
--                 reason, changed, outputs }, which the child sends to the
--                 parent over the result pipe. A plugin that raises an error,
--                 or breaks strict globals, fails with reason plugin-error.
function M.run_step(step)
  local name = step.uses or "run"
  local plugin = ensure_registry()[name]
  if not plugin then error("plugin " .. name .. " is not loaded", 0) end
  local with = step.uses and (step["with"] or cbor.map({})) or { run = step.run }
  local ok, argv, result = pcall(function()
    local mod = open(plugin)
    local remote = step.__qwe
    local ctx = {
      backend = remote and require("backend.ssh").for_target(remote.target, step.env, step.become)
        or require("backend.local").new({ env = step.env, become = step.become }),
    }
    -- sudo must let the step through before anything of it runs
    local why = require("qwe.become").check(ctx.backend)
    if why then
      io.stderr:write("qwe: become refused: ", why, "\n")
      return nil, { status = "failed", reason = "become-denied", changed = false, outputs = {} }
    end
    if type(mod.argv) == "function" then return mod.argv(with, ctx) end
    return nil, require("qwe.checkapply").run(mod, with, ctx)
  end)
  if not ok then
    io.stderr:write("qwe: plugin failed: ", tostring(argv), "\n")
    -- Whether apply ran is not known: count it as changed, like a crash.
    return nil, { status = "failed", reason = "plugin-error", changed = true, outputs = {} }
  end
  return argv, result
end

return M
