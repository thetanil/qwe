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
  if ends_with(kw, "/additionalProperties") and last_token(ptr) == "timeout-minutes" then
    local minutes = resolve(doc, ptr)
    local hint = "timeout-seconds"
    if type(minutes) == "number" then
      hint = string.format("timeout-seconds: %.10g", minutes * 60)
    end
    return {
      pointer = ptr, kind = "key",
      message = 'unknown key "timeout-minutes": the unit is seconds now, write ' .. hint,
    }
  elseif ends_with(kw, "/properties/timeout-seconds/maximum") then
    return { pointer = ptr, kind = "value", message = "timeout-seconds: at most 604800 (seven days)" }
  elseif ends_with(kw, "/additionalProperties") then
    return { pointer = ptr, kind = "key", message = 'unknown key "' .. last_token(ptr) .. '"' }
  elseif ends_with(kw, "/properties/uses/enum") then
    local builtin, project = {}, {}
    for _, p in ipairs(plugin_list) do
      local into = p.project and project or builtin
      into[#into + 1] = p.name
    end
    local known = "built-in: " .. table.concat(builtin, ", ")
    if #project > 0 then known = known .. "; project: " .. table.concat(project, ", ") end
    return {
      pointer = ptr, kind = "value",
      message = 'unknown plugin "' .. tostring(resolve(doc, ptr)) .. '" (' .. known .. ")",
    }
  elseif ends_with(kw, "/properties/on/const") then
    return { pointer = ptr, kind = "value", message = "on: can only be \"local\" (a step runs on its job's target, or on the operator host)" }
  elseif ends_with(kw, "/properties/become/type") then
    return { pointer = ptr, kind = "value", message = "become: must be true (root), a user name or an integer uid" }
  elseif ends_with(kw, "/else/required") then
    return { pointer = ptr, kind = "value", message = "a step needs either run: or uses:" }
  elseif ends_with(kw, "/then/not") then
    return { pointer = ptr, kind = "value", message = "a step cannot have both run: and uses:" }
  elseif ends_with(kw, "/required") then
    return { pointer = ptr, kind = "value", message = err.error or "missing required key" }
  end
  return { pointer = ptr, kind = "value", message = err.error or "invalid value" }
end

-- A job id or step id is a plain name: [A-Za-z_][A-Za-z0-9_-]*, up to 64 chars.
-- (ADR-0009: no regex in the schema, so it is checked here.)
local MAX_ID = 64
local function id_problem(id)
  if #id > MAX_ID then return "is longer than " .. MAX_ID .. " characters" end
  if not id:match("^[A-Za-z_][A-Za-z0-9_%-]*$") then
    return "must match [A-Za-z_][A-Za-z0-9_-]*"
  end
end

-- Checks the schema cannot express. Assumes the schema passed.
local function structure(doc, plugin_list, inventory)
  local inv = require("qwe.inventory")
  local errors = {}
  local ids = {}
  for id in pairs(doc.jobs) do ids[#ids + 1] = id end
  table.sort(ids)
  for _, job_id in ipairs(ids) do
    local seen = {}
    local bad = id_problem(job_id)
    local target = doc.jobs[job_id].target
    if not inv.has_target(inventory, target) then
      errors[#errors + 1] = {
        pointer = "/jobs/" .. esc(job_id) .. "/target",
        kind = "value",
        message = 'unknown target "' .. target .. '" (declared: ' .. table.concat(inv.target_names(inventory), ", ") .. ")",
      }
    end
    if bad then
      errors[#errors + 1] = {
        pointer = "/jobs/" .. esc(job_id),
        kind = "key",
        message = 'job id "' .. job_id .. '" ' .. bad,
      }
    end
    for i, step in ipairs(doc.jobs[job_id].steps) do
      local id = step.id
      if id then
        local step_bad = id_problem(id)
        if step_bad then
          errors[#errors + 1] = {
            pointer = "/jobs/" .. esc(job_id) .. "/steps/" .. (i - 1) .. "/id",
            kind = "value",
            message = 'step id "' .. id .. '" ' .. step_bad,
          }
        end
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
  for _, job_id in ipairs(ids) do
    for i, step in ipairs(doc.jobs[job_id].steps) do
      local at = "/jobs/" .. esc(job_id) .. "/steps/" .. (i - 1) .. "/become"
      local value = step.become
      if type(value) == "string" and not value:match("^[A-Za-z_][A-Za-z0-9_.%-]*$") then
        errors[#errors + 1] = {
          pointer = at, kind = "value",
          message = 'become: "' .. value .. '" is not a user name (use a name like runner, an integer uid, or true for root)',
        }
      elseif type(value) == "number" and value < 0 then
        errors[#errors + 1] = { pointer = at, kind = "value", message = "become: a uid cannot be negative" }
      end
    end
  end
  -- secrets: maps, and a name may be defined in only one place a job can see
  local wf_secrets = type(doc.secrets) == "table" and doc.secrets or {}
  for _, e in ipairs(inv.secret_map_errors(wf_secrets, "/secrets")) do errors[#errors + 1] = e end
  local inv_secrets = (inventory or inv.current()).secrets or {}
  for name in pairs(wf_secrets) do
    if inv_secrets[name] ~= nil then
      errors[#errors + 1] = {
        pointer = "/secrets/" .. esc(name), kind = "key",
        message = 'secret "' .. name .. '" is defined in the workflow and in the inventory',
      }
    end
  end
  for _, job_id in ipairs(ids) do
    local target = doc.jobs[job_id].target
    local t = target ~= "local" and ((inventory or inv.current()).targets or {})[target]
    for name in pairs(t and t.secrets or {}) do
      local where = wf_secrets[name] ~= nil and "the workflow" or inv_secrets[name] ~= nil and "the inventory" or nil
      if where then
        errors[#errors + 1] = {
          pointer = "/jobs/" .. esc(job_id) .. "/target", kind = "value",
          message = 'secret "' .. name .. '" is defined in both ' .. where .. " and target \"" .. target .. "\", which this job uses",
        }
      end
    end
  end
  local outputs = {}
  for _, p in ipairs(plugin_list) do
    if p.outputs then outputs[p.name] = p.outputs end
  end
  for _, e in ipairs(require("qwe.template").check(doc, outputs, inventory)) do errors[#errors + 1] = e end
  return errors
end

-- Returns a list of { pointer, kind, message }, empty if the workflow is valid.
function M.validate(doc, plugin_list, inventory)
  plugin_list = plugin_list or plugins.builtin()
  inventory = inventory or require("qwe.inventory").current()
  local result = kernel_validator(plugin_list):validate(doc)
  local errors = {}
  if result.valid then
    return structure(doc, plugin_list, inventory)
  end
  for _, err in ipairs(result.errors or {}) do
    -- The allOf line only says that some branch inside it failed.
    if not (ends_with(err.keywordLocation, "/allOf") or err.keywordLocation == "/allOf") then
      errors[#errors + 1] = describe(doc, err, plugin_list)
    end
  end
  return errors
end

-- Checks data against any schema (a decoded table). Returns a list of
-- { pointer, kind, message }, empty if the data is valid.
function M.check(schema_tbl, data)
  local result = schema.new(schema_tbl):validate(data)
  local list = {}
  if result.valid then return list end
  for _, err in ipairs(result.errors or {}) do
    local kw = err.keywordLocation
    -- These lines only say that some branch inside them failed.
    if not (ends_with(kw, "/allOf") or ends_with(kw, "/anyOf") or ends_with(kw, "/oneOf")) then
      if ends_with(kw, "/additionalProperties") then
        list[#list + 1] = {
          pointer = err.instanceLocation, kind = "key",
          message = 'unknown key "' .. last_token(err.instanceLocation) .. '"',
        }
      else
        list[#list + 1] = { pointer = err.instanceLocation, kind = "value", message = err.error or "invalid value" }
      end
    end
  end
  return list
end

-- Loads the project's plugins (next to the workflow, in dir) and validates doc
-- against the built-in and project plugins together. Returns the workflow's
-- errors and the plugins' problems ({ where, message }).
function M.validate_project(doc, dir, inventory)
  local list, problems = plugins.load(dir)
  return M.validate(doc, list, inventory), problems
end

return M
