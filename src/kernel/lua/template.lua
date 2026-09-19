-- Templating and step outputs. `${{ env.X }}` and `${{ steps.<id>.outputs.<k> }}`
-- are the only expressions (ticket 11; vars and secrets come later). They are
-- checked when the workflow is validated and evaluated in the parent just
-- before each step starts.
--
-- env values may use only steps references: an env value that read env would
-- need an evaluation order, and nothing needs one.
local cbor = require("qwe.cbor")
local json = require("dkjson")

local M = {}

local NAME = "[A-Za-z_][A-Za-z0-9_%-]*"

-- Parses the inside of ${{ }}. Returns { kind = "env", name } or
-- { kind = "steps", step, key }, or nil and a message.
local function parse_expr(expr)
  local name = expr:match("^env%.([A-Za-z_][A-Za-z0-9_]*)$")
  if name then return { kind = "env", name = name } end
  local step, key = expr:match("^steps%.(" .. NAME .. ")%.outputs%.(" .. NAME .. ")$")
  if step then return { kind = "steps", step = step, key = key } end
  return nil, 'unknown expression "' .. expr .. '" (only env.NAME and steps.ID.outputs.KEY exist)'
end

-- Calls fn(expr_text) for every ${{ }} in s, in order.
local function each_expr(s, fn)
  for inner in s:gmatch("%${{(.-)}}") do
    fn((inner:gsub("^%s+", ""):gsub("%s+$", "")))
  end
end

-- Replaces every ${{ }} in s with what resolve(parsed) returns.
local function substitute(s, resolve)
  return (s:gsub("%${{(.-)}}", function(inner)
    local parsed = parse_expr((inner:gsub("^%s+", ""):gsub("%s+$", "")))
    return parsed and resolve(parsed) or ""
  end))
end

local function esc(token)
  return (tostring(token):gsub("~", "~0"):gsub("/", "~1"))
end

-- Problems with the templates and env names of a decoded workflow. Returns a
-- list of { pointer, kind = "value", message }. plugins maps a plugin name to
-- its declared output names, when it has any.
function M.check(doc, plugin_outputs)
  local errors = {}
  local function add(pointer, message)
    errors[#errors + 1] = { pointer = pointer, kind = "value", message = message }
  end

  -- earlier: step id -> the plugin's outputs table or true, for the steps before this one
  local function scan(value, pointer, earlier, in_env)
    if type(value) == "string" then
      each_expr(value, function(expr)
        local parsed, message = parse_expr(expr)
        if not parsed then return add(pointer, message) end
        if parsed.kind == "env" and in_env then
          return add(pointer, "an env value cannot read env: ${{ " .. expr .. " }}")
        end
        if parsed.kind == "steps" then
          local step = earlier[parsed.step]
          if not step then
            return add(pointer, 'no step "' .. parsed.step .. '" before this one in the job: ${{ ' .. expr
              .. " }} (outputs are visible only within the same job, to later steps)")
          end
          if type(step) == "table" and step[parsed.key] == nil then
            add(pointer, 'step "' .. parsed.step .. '" declares no output "' .. parsed.key .. '"')
          end
        end
      end)
    elseif type(value) == "table" then
      if getmetatable(value) == cbor.array_mt then
        for i, v in ipairs(value) do scan(v, pointer .. "/" .. (i - 1), earlier, in_env) end
      else
        for k, v in pairs(value) do scan(v, pointer .. "/" .. esc(k), earlier, in_env) end
      end
    end
  end

  local function check_env(env, pointer, earlier)
    if type(env) ~= "table" then return end
    for name, value in pairs(env) do
      if not name:match("^[A-Za-z_][A-Za-z0-9_]*$") then
        errors[#errors + 1] = {
          pointer = pointer .. "/" .. esc(name),
          kind = "key",
          message = 'env name "' .. name .. '" must match [A-Za-z_][A-Za-z0-9_]*',
        }
      elseif name == "QWE_OUTPUT" then
        errors[#errors + 1] = {
          pointer = pointer .. "/" .. esc(name),
          kind = "key",
          message = "QWE_OUTPUT is set by qwe for run: steps",
        }
      elseif type(value) == "string" then
        scan(value, pointer .. "/" .. esc(name), earlier, true)
      end
    end
  end

  check_env(doc.env, "/env", {})
  local job_ids = {}
  for id in pairs(doc.jobs) do job_ids[#job_ids + 1] = id end
  table.sort(job_ids)
  for _, job_id in ipairs(job_ids) do
    local job = doc.jobs[job_id]
    local base = "/jobs/" .. esc(job_id)
    check_env(job.env, base .. "/env", {})
    local earlier = {}
    for i, step in ipairs(job.steps) do
      local at = base .. "/steps/" .. (i - 1)
      check_env(step.env, at .. "/env", earlier)
      if type(step.run) == "string" then scan(step.run, at .. "/run", earlier, false) end
      if type(step["with"]) == "table" then scan(step["with"], at .. "/with", earlier, false) end
      if step.id then
        earlier[step.id] = (step.uses and plugin_outputs[step.uses]) or true
      end
    end
  end
  return errors
end

local function copy(value, resolve)
  if type(value) == "string" then return substitute(value, resolve) end
  if type(value) ~= "table" then return value end
  local out = {}
  for k, v in pairs(value) do out[k] = copy(v, resolve) end
  return setmetatable(out, getmetatable(value))
end

-- Env values are strings in the process environment.
local function env_string(v)
  if type(v) == "number" and v == math.floor(v) then return string.format("%d", v) end
  return tostring(v)
end

-- The step as the child runs it: templates evaluated, and env = the merged
-- declared env (workflow, then job, then step; the innermost wins) as strings.
-- outputs is the job's { step id -> { key -> value } }. output_path, for a
-- run: step, is the file its outputs are written to ($QWE_OUTPUT).
function M.resolve(workflow, job, index, outputs, output_path)
  local step = job.steps[index]
  local function resolve(parsed)
    if parsed.kind == "env" then
      -- only reachable in run: text and with: values; env values have no env refs
      return nil
    end
    local by_step = outputs[parsed.step]
    local value = by_step and by_step[parsed.key]
    if value == nil then return nil end
    return env_string(value)
  end
  local env = {}
  for _, scope in ipairs({ workflow.env or {}, job.env or {}, step.env or {} }) do
    for name, value in pairs(scope) do env[name] = env_string(value) end
  end
  -- env references in run: text and with: values read the merged env
  local function resolve_all(parsed)
    if parsed.kind == "env" then return env[parsed.name] end
    return resolve(parsed)
  end
  local resolved = {}
  for k, v in pairs(step) do
    if k ~= "env" then resolved[k] = copy(v, resolve_all) else resolved[k] = v end
  end
  -- env values: steps references only
  local final = {}
  for name, value in pairs(env) do
    final[name] = substitute(value, function(parsed)
      if parsed.kind == "steps" then return resolve(parsed) end
    end)
  end
  if step.run ~= nil and output_path then final.QWE_OUTPUT = output_path end
  resolved.env = final
  return setmetatable(resolved, getmetatable(step))
end

-- Parses a $QWE_OUTPUT file (GitHub's format): key=value lines, and
-- key<<DELIM ... DELIM for a value of several lines. A line that is neither
-- is ignored.
function M.parse_output(text)
  local out = {}
  local lines = {}
  for line in (text .. "\n"):gmatch("(.-)\r?\n") do lines[#lines + 1] = line end
  if lines[#lines] == "" then lines[#lines] = nil end
  local i = 1
  while i <= #lines do
    local line = lines[i]
    local key, delim = line:match("^(" .. NAME .. ")<<(.+)$")
    if key then
      local parts = {}
      i = i + 1
      while i <= #lines and lines[i] ~= delim do
        parts[#parts + 1] = lines[i]
        i = i + 1
      end
      if i <= #lines then out[key] = table.concat(parts, "\n") end
    else
      local k, v = line:match("^(" .. NAME .. ")=(.*)$")
      if k then out[k] = v end
    end
    i = i + 1
  end
  return out
end

-- Reads and deletes the run: step's output file. Returns its outputs, or an
-- empty table if the step wrote none.
function M.take_output_file(path)
  local f = io.open(path, "rb")
  if not f then return {} end
  local text = f:read("*a")
  f:close()
  os.remove(path)
  return M.parse_output(text)
end

-- Records a step's outputs for later steps of the job (under its id, if it has
-- one). Returns the outputs as a JSON object with sorted keys, or nil if there
-- are none, for result.json.
function M.store(outputs, id, tbl)
  if type(tbl) ~= "table" then return nil end
  local keys = {}
  local copy_of = {}
  for k, v in pairs(tbl) do
    if type(k) == "string" and (type(v) == "string" or type(v) == "number" or type(v) == "boolean") then
      keys[#keys + 1] = k
      copy_of[k] = v
    end
  end
  if #keys == 0 then return nil end
  table.sort(keys)
  if id then outputs[id] = copy_of end
  return json.encode(copy_of, { keyorder = keys })
end

return M
