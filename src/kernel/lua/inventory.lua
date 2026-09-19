-- The inventory reader (a service, ticket 12): reads the operator's
-- description of the world, validates it, and answers what a job's target
-- holds. Nothing is inherited: there are no groups, defaults or cascades, and
-- a key that says otherwise is rejected as unknown by the schema.
local cbor = require("qwe.cbor")
local plugins = require("qwe.plugins")
local validate = require("qwe.validate")

local M = {}

local NAME = "^[A-Za-z_][A-Za-z0-9_%-]*$"

local function esc(token)
  return (tostring(token):gsub("~", "~0"):gsub("/", "~1"))
end

local current = { targets = {} }

-- Makes inv (a decoded, validated inventory) the one jobs read from.
function M.use(inv)
  current = inv or { targets = {} }
  current.targets = current.targets or {}
end

function M.current()
  return current
end

-- The names a job's target: may use.
function M.target_names(inv)
  local names = { "local" }
  for name in pairs((inv or current).targets or {}) do names[#names + 1] = name end
  table.sort(names, function(a, b)
    if a == "local" then return b ~= "local" end
    if b == "local" then return false end
    return a < b
  end)
  return names
end

function M.has_target(inv, name)
  return name == "local" or ((inv or current).targets or {})[name] ~= nil
end

-- The vars of a target, or nil if there is no such target. `local` has none.
function M.vars(target, inv)
  if target == "local" then return {} end
  local t = ((inv or current).targets or {})[target]
  if not t then return nil end
  return t.vars or {}
end

-- How many sessions (concurrent jobs) an ssh target takes; ssh servers cap a
-- multiplexed connection near 10, so the default stays under it.
M.DEFAULT_MAX_SESSIONS = 8

function M.max_sessions(target, inv)
  local t = ((inv or current).targets or {})[target]
  return t and t["max-sessions"] or M.DEFAULT_MAX_SESSIONS
end

-- The host: of a target, or nil.
function M.host(target, inv)
  local t = ((inv or current).targets or {})[target]
  return t and t.host
end

-- Every !encrypted value in doc that is not the value of an entry of a
-- secrets: map (the root's, or a target's). allowed(tokens) says whether the
-- path (a list of keys) is inside one.
local function envelopes_outside(doc, allowed)
  local found = {}
  local function walk(value, tokens, pointer)
    if type(value) ~= "table" then return end
    if cbor.is_secret(value) then
      if not allowed(tokens) then found[#found + 1] = pointer end
    elseif getmetatable(value) == cbor.array_mt then
      for i, v in ipairs(value) do
        tokens[#tokens + 1] = i - 1
        walk(v, tokens, pointer .. "/" .. (i - 1))
        tokens[#tokens] = nil
      end
    else
      local keys = {}
      for k in pairs(value) do keys[#keys + 1] = k end
      table.sort(keys)
      for _, k in ipairs(keys) do
        tokens[#tokens + 1] = k
        walk(value[k], tokens, pointer .. "/" .. esc(k))
        tokens[#tokens] = nil
      end
    end
  end
  walk(doc, {}, "")
  return found
end

local ENVELOPE_MESSAGE = "an !encrypted value can only be an entry of a secrets: map"

-- Errors for !encrypted values outside a secrets: map, as { pointer, kind,
-- message }.
function M.envelope_errors(doc)
  local errors = {}
  local function allowed(tokens)
    if tokens[1] == "secrets" then return #tokens == 2 end
    return tokens[1] == "targets" and tokens[3] == "secrets" and #tokens == 4
  end
  for _, pointer in ipairs(envelopes_outside(doc, allowed)) do
    errors[#errors + 1] = { pointer = pointer, kind = "value", message = ENVELOPE_MESSAGE }
  end
  return errors
end

-- Returns a list of { pointer, kind, message } for a decoded inventory, empty
-- if it is valid.
function M.validate(inv)
  local errors = M.envelope_errors(inv)
  local flagged = {}
  for _, e in ipairs(errors) do flagged[e.pointer] = true end
  local schema_tbl = plugins.decode_schema(require("inventory_schema"))
  for _, e in ipairs(validate.check(schema_tbl, inv)) do
    if e.pointer:match("^/targets/[^/]+/backend$") then
      e.message = 'unknown backend "' .. tostring(inv.targets[e.pointer:match("^/targets/([^/]+)/")].backend) .. '" (supported: ssh)'
    end
    -- the same value, already reported as an envelope in the wrong place
    if not flagged[e.pointer] then errors[#errors + 1] = e end
  end
  local targets = type(inv.targets) == "table" and inv.targets or {}
  if targets["local"] ~= nil then
    errors[#errors + 1] = {
      pointer = "/targets/local", kind = "key",
      message = '"local" is built in and cannot be defined in the inventory',
    }
  end
  for name, t in pairs(targets) do
    if type(t) == "table" and type(t.vars) == "table" then
      for var in pairs(t.vars) do
        if not var:match(NAME) then
          errors[#errors + 1] = {
            pointer = "/targets/" .. esc(name) .. "/vars/" .. esc(var), kind = "key",
            message = 'var name "' .. var .. '" must match [A-Za-z_][A-Za-z0-9_-]*',
          }
        end
      end
    end
  end
  return errors
end

return M
