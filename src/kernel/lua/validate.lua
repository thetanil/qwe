-- Validates a decoded workflow against the schema the kernel composes from its
-- own structure plus each plugin's with: schema (ADR-0009).
local cbor = require("qwe.cbor")
local plugins = require("qwe.plugins")
require("compat53")
local schema = require("schema")
require("schema.draft-07")

schema.output_format = "basic"
schema.json.is_array = function(d) return getmetatable(d) == cbor.array_mt end
schema.json.is_object = function(d) return getmetatable(d) == cbor.map_mt end
schema.json.null = cbor.null

local M = {}

local function esc(token)
  return (tostring(token):gsub("~", "~0"):gsub("/", "~1"))
end

local function unesc(token)
  return (token:gsub("~1", "/"):gsub("~0", "~"))
end

local function last_token(pointer)
  return unesc(pointer:match("([^/]*)$"))
end

-- The value a JSON Pointer points to, or nil.
local function resolve(doc, pointer)
  local cur = doc
  for token in pointer:gmatch("/([^/]*)") do
    if type(cur) ~= "table" then return nil end
    token = unesc(token)
    if getmetatable(cur) == cbor.array_mt then
      cur = cur[(tonumber(token) or -1) + 1]
    else
      cur = cur[token]
    end
  end
  return cur
end

local function ends_with(s, suffix)
  return s:sub(-#suffix) == suffix
end

-- Builds the composed schema: the kernel's, with the step definition extended
-- by an `if uses == X then with: <X's schema>` branch per plugin. It is an
-- allOf of if/then, not a oneOf, so an error comes only from the branch that
-- matches and never from the other plugins' branches.
function M.compose(kernel_schema, plugin_list)
  local step = kernel_schema.definitions.step
  local names = cbor.array({})
  for _, p in ipairs(plugin_list) do
    names[#names + 1] = p.name
    local then_ = cbor.map({ properties = cbor.map({ with = p.schema }) })
    local req = p.schema.required
    if req and #req > 0 then
      then_.required = cbor.array({ "with" })
    end
    step.allOf[#step.allOf + 1] = cbor.map({
      ["if"] = cbor.map({
        properties = cbor.map({ uses = cbor.map({ const = p.name }) }),
        required = cbor.array({ "uses" }),
      }),
      ["then"] = then_,
    })
  end
  step.properties.uses.enum = names
  return kernel_schema
end

local function kernel_validator(plugin_list)
  local kernel = plugins.decode_schema(require("workflow_schema"))
  return schema.new(M.compose(kernel, plugin_list))
end

-- Turns one lua-schema error into { pointer, kind, message }. kind says which
-- position to report: "key" for an unknown key, "value" otherwise.
local function describe(doc, err, plugin_list)
  local kw = err.keywordLocation
  local ptr = err.instanceLocation
  if ends_with(kw, "/additionalProperties") then
    return { pointer = ptr, kind = "key", message = 'unknown key "' .. last_token(ptr) .. '"' }
  elseif ends_with(kw, "/properties/uses/enum") then
    local names = {}
    for _, p in ipairs(plugin_list) do names[#names + 1] = p.name end
    return {
      pointer = ptr, kind = "value",
      message = 'unknown plugin "' .. tostring(resolve(doc, ptr)) .. '" (built-in: ' .. table.concat(names, ", ") .. ")",
    }
  elseif ends_with(kw, "/properties/on/const") then
    return { pointer = ptr, kind = "value", message = "on: can only be \"local\" (a step runs on its job's target, or on the operator host)" }
  elseif ends_with(kw, "/else/required") then
    return { pointer = ptr, kind = "value", message = "a step needs either run: or uses:" }
  elseif ends_with(kw, "/then/not") then
    return { pointer = ptr, kind = "value", message = "a step cannot have both run: and uses:" }
  elseif ends_with(kw, "/required") then
    return { pointer = ptr, kind = "value", message = err.error or "missing required key" }
  end
  return { pointer = ptr, kind = "value", message = err.error or "invalid value" }
end

-- Checks the schema cannot express. Assumes the schema passed.
local function structure(doc)
  local errors = {}
  local ids = {}
  for id in pairs(doc.jobs) do ids[#ids + 1] = id end
  table.sort(ids)
  for _, job_id in ipairs(ids) do
    local seen = {}
    for i, step in ipairs(doc.jobs[job_id].steps) do
      local id = step.id
      if id then
        if seen[id] then
          errors[#errors + 1] = {
            pointer = "/jobs/" .. esc(job_id) .. "/steps/" .. (i - 1) .. "/id",
            kind = "value",
            message = 'duplicate step id "' .. id .. '" (first used by step ' .. seen[id] .. ")",
          }
        else
          seen[id] = i - 1
        end
      end
    end
  end
  return errors
end

-- Returns a list of { pointer, kind, message }, empty if the workflow is valid.
function M.validate(doc, plugin_list)
  plugin_list = plugin_list or plugins.builtin()
  local result = kernel_validator(plugin_list):validate(doc)
  local errors = {}
  if result.valid then
    return structure(doc)
  end
  for _, err in ipairs(result.errors or {}) do
    -- The allOf line only says that some branch inside it failed.
    if not (ends_with(err.keywordLocation, "/allOf") or err.keywordLocation == "/allOf") then
      errors[#errors + 1] = describe(doc, err, plugin_list)
    end
  end
  return errors
end

return M
