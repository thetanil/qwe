-- Templating and step outputs. `${{ env.X }}`, `${{ steps.<id>.outputs.<k> }}`,
-- `${{ vars.X }}` (a value of the job's target) and `${{ secrets.X }}` are the only
-- expressions. They are checked when the workflow is validated and evaluated in the parent just
-- before each step starts.
--
-- env values may use only steps, vars and secrets references: an env value that read env
-- would need an evaluation order, and nothing needs one.
local cbor = require("qwe.cbor")
local json = require("dkjson")

local M = {}

local NAME = "[A-Za-z_][A-Za-z0-9_%-]*"

-- Parses the inside of ${{ }}. Returns { kind = "env", name }, { kind = "vars", name },
-- { kind = "secrets", name } or
-- { kind = "steps", step, key }, or nil and a message.
local function parse_expr(expr)
  local name = expr:match("^env%.([A-Za-z_][A-Za-z0-9_]*)$")
  if name then return { kind = "env", name = name } end
  local var = expr:match("^vars%.(" .. NAME .. ")$")
  if var then return { kind = "vars", name = var } end
  local secret = expr:match("^secrets%.(" .. NAME .. ")$")
  if secret then return { kind = "secrets", name = secret } end
  local step, key = expr:match("^steps%.(" .. NAME .. ")%.outputs%.(" .. NAME .. ")$")
  if step then return { kind = "steps", step = step, key = key } end
  return nil, 'unknown expression "' .. expr .. '" (only env.NAME, vars.NAME, secrets.NAME and steps.ID.outputs.KEY exist)'
end

-- Calls fn(expr_text) for every ${{ }} in s, in order.
local function each_expr(s, fn)
  for inner in s:gmatch("%${{(.-)}}") do
    fn((inner:gsub("^%s+", ""):gsub("%s+$", "")))
  end
end

-- Replaces every ${{ }} in s with what resolve(parsed) returns. A result that
-- came from a secret (a secrets expression, or a resolver that returns true as
-- its second value) makes the whole string secret: it is added to the run's
-- redaction set, so it is masked if it is ever printed. Returns the string.
local function substitute(s, resolve)
  local tainted = false
  local out = s:gsub("%${{(.-)}}", function(inner)
    local parsed = parse_expr((inner:gsub("^%s+", ""):gsub("%s+$", "")))
    if not parsed then return "" end
    local value, secret = resolve(parsed)
    if parsed.kind == "secrets" or secret then tainted = true end
    return value or ""
  end)
  if tainted then require("qwe.secrets").mask(out) end
  return out
end

local function esc(token)
  return (tostring(token):gsub("~", "~0"):gsub("/", "~1"))
end

-- Problems with the templates and env names of a decoded workflow. Returns a
-- list of { pointer, kind = "value", message }. plugins maps a plugin name to
-- its declared output names, when it has any.
function M.check(doc, plugin_outputs, inventory)
  local inv = require("qwe.inventory")
  local errors = {}
  local function add(pointer, message)
    errors[#errors + 1] = { pointer = pointer, kind = "value", message = message }
  end

  -- earlier: step id -> the plugin's outputs table or true, for the steps before this one
  local function scan(value, pointer, earlier, in_env, target, is_run)
    if type(value) == "string" then
      each_expr(value, function(expr)
        local parsed, message = parse_expr(expr)
        if not parsed then return add(pointer, message) end
        if parsed.kind == "env" and in_env then
          return add(pointer, "an env value cannot read env: ${{ " .. expr .. " }}")
        end
        if parsed.kind == "secrets" then
          if is_run then
            return add(pointer, "a secret cannot be substituted into run: text, where it would be in the command line of every process on the host: map it with env: (env: { NAME: ${{ " .. expr .. " }} }) and read $NAME")
          end
          local visible = inv.secrets_for(target, inventory)
          for name, s in pairs(type(doc.secrets) == "table" and doc.secrets or {}) do visible[name] = s end
          if visible[parsed.name] == nil then
            local names = {}
            for k in pairs(visible) do names[#names + 1] = k end
            table.sort(names)
            return add(pointer, 'no secret "' .. parsed.name .. '" is visible here'
              .. (#names > 0 and (" (secrets: " .. table.concat(names, ", ") .. ")") or " (none are defined)"))
          end
        end
        if parsed.kind == "vars" then
          if target == nil then
            return add(pointer, "vars belong to a job's target: use them in a job or step, or map them in a job's env: (${{ " .. expr .. " }})")
          end
          local vars = inv.vars(target, inventory)
          if vars and vars[parsed.name] == nil then
            local names = {}
            for k in pairs(vars) do names[#names + 1] = k end
            table.sort(names)
            return add(pointer, 'target "' .. target .. '" has no var "' .. parsed.name .. '"'
              .. (#names > 0 and (" (vars: " .. table.concat(names, ", ") .. ")") or " (it has no vars)"))
          end
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
        for i, v in ipairs(value) do scan(v, pointer .. "/" .. (i - 1), earlier, in_env, target) end
      else
        for k, v in pairs(value) do scan(v, pointer .. "/" .. esc(k), earlier, in_env, target) end
      end
    end
  end

  local function check_env(env, pointer, earlier, target)
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
      elseif name == "QWE_STEP" then
        errors[#errors + 1] = {
          pointer = pointer .. "/" .. esc(name),
          kind = "key",
          message = "QWE_STEP is set by qwe for steps on a remote target",
        }
      elseif type(value) == "string" then
        scan(value, pointer .. "/" .. esc(name), earlier, true, target)
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
    check_env(job.env, base .. "/env", {}, job.target)
    local earlier = {}
    for i, step in ipairs(job.steps) do
      local at = base .. "/steps/" .. (i - 1)
      check_env(step.env, at .. "/env", earlier, job.target)
      if type(step.run) == "string" then scan(step.run, at .. "/run", earlier, false, job.target, true) end
      if type(step["with"]) == "table" then scan(step["with"], at .. "/with", earlier, false, job.target) end
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
-- A step that runs on a remote target (the job's is not local, and the step
-- does not say on: local) gets QWE_STEP=token in its environment, which is how
-- qwe finds what the step started there (backend.ssh), and __qwe.target for
-- the backend to pick.
function M.resolve(workflow, job, index, outputs, output_path, token)
  local step = job.steps[index]
  local inventory = require("qwe.inventory")
  local vars = inventory.vars(job.target) or {}
  -- the secrets this job can name; decrypted (in this, the parent) only when a
  -- value that uses one is made, and from then on masked in every step's output
  local secret_scope = inventory.secrets_for(job.target)
  for name, s in pairs(workflow.secrets or {}) do secret_scope[name] = s end
  local function resolve(parsed)
    if parsed.kind == "vars" then
      local value = vars[parsed.name]
      if value == nil then return nil end
      return env_string(value)
    end
    if parsed.kind == "secrets" then
      local secret = secret_scope[parsed.name]
      if secret == nil then return nil end
      return require("qwe.secrets").reveal(secret)
    end
    if parsed.kind == "env" then return nil end
    local by_step = outputs[parsed.step]
    local value = by_step and by_step[parsed.key]
    if value == nil then return nil end
    return env_string(value)
  end
  local env = {}
  for _, scope in ipairs({ workflow.env or {}, job.env or {}, step.env or {} }) do
    for name, value in pairs(scope) do env[name] = env_string(value) end
  end
  -- env values: steps, vars and secrets references (never env). One built from
  -- a secret is secret as a whole.
  local final, from_secret = {}, {}
  for name, value in pairs(env) do
    final[name] = substitute(value, function(parsed)
      if parsed.kind == "secrets" then from_secret[name] = true end
      if parsed.kind ~= "env" then return resolve(parsed) end
    end)
  end
  -- env references in run: text and with: values read the finished env; a
  -- string that reads one built from a secret is secret too
  local function resolve_all(parsed)
    if parsed.kind == "env" then return final[parsed.name], from_secret[parsed.name] end
    return resolve(parsed)
  end
  local resolved = {}
  for k, v in pairs(step) do
    if k ~= "env" then resolved[k] = copy(v, resolve_all) else resolved[k] = v end
  end
  local remote = job.target ~= nil and job.target ~= "local" and step.on ~= "local"
  -- the output file is on the operator host: a remote run: step cannot write it yet
  if step.run ~= nil and output_path and not remote then final.QWE_OUTPUT = output_path end
  if remote then
    final.QWE_STEP = token
    resolved.__qwe = { target = job.target }
  end
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
-- one). step is the step being run. An output is secret if the step lists it in
-- secret-outputs: or its plugin declares it secret: its value joins the run's
-- redaction set at once, later steps of the job can still read it, and it is
-- left out of what this returns: the outputs as a JSON object with sorted keys
-- for result.json, or nil if there are none to show.
function M.store(outputs, step, tbl)
  if type(tbl) ~= "table" then return nil end
  local secret = {}
  for _, k in ipairs(step["secret-outputs"] or {}) do secret[k] = true end
  local declared = step.uses and require("qwe.plugins").outputs_of(step.uses)
  for k, decl in pairs(declared or {}) do
    if type(decl) == "table" and decl.secret == true then secret[k] = true end
  end
  local shown, copy_of, any = {}, {}, false
  for k, v in pairs(tbl) do
    if type(k) == "string" and (type(v) == "string" or type(v) == "number" or type(v) == "boolean") then
      any = true
      copy_of[k] = v
      if secret[k] then
        require("qwe.secrets").mask(env_string(v))
      else
        shown[#shown + 1] = k
      end
    end
  end
  if not any then return nil end
  if step.id then outputs[step.id] = copy_of end
  if #shown == 0 then return nil end
  table.sort(shown)
  local visible = {}
  for _, k in ipairs(shown) do visible[k] = copy_of[k] end
  return json.encode(visible, { keyorder = shown })
end

return M
